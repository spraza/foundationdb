/*
 * SingleDecreePaxos.cpp
 *
 * This source file is part of the FoundationDB open source project
 *
 * Copyright 2013-2026 Apple Inc. and the FoundationDB project authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fdbserver/core/SingleDecreePaxos.h"

#include <map>

#include "fdbclient/WellKnownEndpoints.h"
#include "fdbserver/core/Knobs.h"
#include "flow/CoroUtils.h"
#include "flow/UnitTest.h"

PaxosPrepareTransition paxosPrepare(PaxosAcceptorState& state, PaxosBallot ballot, bool observeOnly) {
	if (observeOnly) {
		return { PaxosPrepareReply(ballot >= state.promised, state), false };
	}

	if (ballot < state.promised) {
		return { PaxosPrepareReply(false, state), false };
	}

	bool changed = ballot > state.promised;
	state.promised = ballot;
	ASSERT(!state.accepted.present() || state.accepted.get().ballot <= state.promised);
	return { PaxosPrepareReply(true, state), changed };
}

PaxosAcceptTransition paxosAccept(PaxosAcceptorState& state, PaxosBallot ballot, Value value) {
	if (ballot < state.promised) {
		return { PaxosAcceptReply(false, state.promised), false };
	}

	if (state.accepted.present() && state.accepted.get().ballot == ballot) {
		ASSERT(state.accepted.get().value == value);
		return { PaxosAcceptReply(true, state.promised), false };
	}

	state.promised = ballot;
	state.accepted = PaxosAcceptedValue(ballot, std::move(value));
	ASSERT(state.accepted.get().ballot <= state.promised);
	return { PaxosAcceptReply(true, state.promised), true };
}

namespace {

Future<PaxosPrepareReply> sendPrepare(PaxosAcceptorInterface acceptor, PaxosPrepareRequest request) {
	if (SERVER_KNOBS->BUGGIFY_ALL_COORDINATION || buggify()) {
		co_await delay(SERVER_KNOBS->BUGGIFIED_EVENTUAL_CONSISTENCY * deterministicRandom()->random01());
	}

	PaxosPrepareReply reply;
	if (acceptor.hostname.present()) {
		reply = co_await retryGetReplyFromHostname(request, acceptor.hostname.get(), WLTOKEN_GENERATIONREG_READ);
	} else {
		reply = co_await retryBrokenPromise(acceptor.prepare, request);
	}

	if (SERVER_KNOBS->BUGGIFY_ALL_COORDINATION || buggify()) {
		co_await delay(SERVER_KNOBS->BUGGIFIED_EVENTUAL_CONSISTENCY * deterministicRandom()->random01());
	}
	co_return reply;
}

Future<PaxosAcceptReply> sendAccept(PaxosAcceptorInterface acceptor, PaxosAcceptRequest request) {
	if (SERVER_KNOBS->BUGGIFY_ALL_COORDINATION || buggify()) {
		co_await delay(SERVER_KNOBS->BUGGIFIED_EVENTUAL_CONSISTENCY * deterministicRandom()->random01());
	}

	PaxosAcceptReply reply;
	if (acceptor.hostname.present()) {
		reply = co_await retryGetReplyFromHostname(request, acceptor.hostname.get(), WLTOKEN_GENERATIONREG_WRITE);
	} else {
		reply = co_await retryBrokenPromise(acceptor.accept, request);
	}

	if (SERVER_KNOBS->BUGGIFY_ALL_COORDINATION || buggify()) {
		co_await delay(SERVER_KNOBS->BUGGIFIED_EVENTUAL_CONSISTENCY * deterministicRandom()->random01());
	}
	co_return reply;
}

Future<Void> prepareHasResult(Future<PaxosPrepareReply> reply, bool promised) {
	PaxosPrepareReply result = co_await reply;
	if (result.promised != promised) {
		co_await Future<Void>(Never());
	}
}

Future<Void> acceptHasResult(Future<PaxosAcceptReply> reply, bool accepted) {
	PaxosAcceptReply result = co_await reply;
	if (result.accepted != accepted) {
		co_await Future<Void>(Never());
	}
}

Future<Void> prepareShowsPreemption(Future<PaxosPrepareReply> reply, PaxosBallot ballot) {
	PaxosPrepareReply value = co_await reply;
	if (value.promisedBallot <= ballot) {
		co_await Future<Void>(Never());
	}
}

void recordChosen(KeyRef key, uint64_t instance, ValueRef value) {
	if (!g_network->isSimulated()) {
		return;
	}

	static std::map<std::pair<Key, uint64_t>, Value> chosen;
	auto [it, inserted] = chosen.emplace(std::make_pair(Key(key), instance), Value(value));
	ASSERT(inserted || it->second == value);
}

} // namespace

Future<PaxosPrepareQuorum> paxosPrepareQuorum(std::vector<PaxosAcceptorInterface> const& acceptors,
                                              Key key,
                                              uint64_t instance,
                                              UID proposer,
                                              uint64_t minimumRound) {
	ASSERT(!acceptors.empty());
	int const quorumSize = acceptors.size() / 2 + 1;
	int const rejectionThreshold = acceptors.size() - quorumSize + 1;
	PaxosBallot ballot(std::max<uint64_t>(minimumRound, 1), proposer);

	while (true) {
		std::vector<Future<PaxosPrepareReply>> replies;
		std::vector<Future<Void>> promises;
		std::vector<Future<Void>> rejections;
		for (auto const& acceptor : acceptors) {
			Future<PaxosPrepareReply> reply = sendPrepare(acceptor, PaxosPrepareRequest(key, instance, ballot));
			replies.push_back(reply);
			promises.push_back(prepareHasResult(reply, true));
			rejections.push_back(prepareHasResult(reply, false));
		}

		Future<Void> promised = quorum(promises, quorumSize);
		Future<Void> rejected = quorum(rejections, rejectionThreshold);
		auto result = co_await race(promised, rejected);

		PaxosBallot highestPromised = ballot;
		Optional<PaxosAcceptedValue> highestAccepted;
		for (auto const& reply : replies) {
			if (!reply.isReady() || reply.isError()) {
				continue;
			}
			PaxosPrepareReply const& value = reply.get();
			highestPromised = std::max(highestPromised, value.promisedBallot);
			if (value.promised && value.accepted.present() &&
			    (!highestAccepted.present() || highestAccepted.get().ballot < value.accepted.get().ballot)) {
				highestAccepted = value.accepted;
			}
		}

		if (result.index() == 0) {
			co_return PaxosPrepareQuorum{ ballot, highestAccepted, highestPromised.round };
		}

		ballot = PaxosBallot(std::max(ballot.round, highestPromised.round) + 1, proposer);
		co_await delay(0.001 + deterministicRandom()->random01() * 0.01);
	}
}

Future<PaxosAcceptQuorum> paxosAcceptQuorum(std::vector<PaxosAcceptorInterface> const& acceptors,
                                            Key key,
                                            uint64_t instance,
                                            PaxosBallot ballot,
                                            Value value,
                                            bool requireAll) {
	ASSERT(!acceptors.empty());
	int const required = requireAll ? acceptors.size() : acceptors.size() / 2 + 1;
	int const rejectionThreshold = acceptors.size() - required + 1;
	std::vector<Future<PaxosAcceptReply>> replies;
	std::vector<Future<Void>> accepted;
	std::vector<Future<Void>> rejected;
	for (auto const& acceptor : acceptors) {
		Future<PaxosAcceptReply> reply = sendAccept(acceptor, PaxosAcceptRequest(key, instance, ballot, value));
		replies.push_back(reply);
		accepted.push_back(acceptHasResult(reply, true));
		rejected.push_back(acceptHasResult(reply, false));
	}

	Future<Void> acceptedQuorum = quorum(accepted, required);
	Future<Void> rejectedQuorum = quorum(rejected, rejectionThreshold);
	auto result = co_await race(acceptedQuorum, rejectedQuorum);

	PaxosBallot highestPromised = ballot;
	for (auto const& reply : replies) {
		if (reply.isReady() && !reply.isError()) {
			highestPromised = std::max(highestPromised, reply.get().promisedBallot);
		}
	}

	if (result.index() == 0) {
		recordChosen(key, instance, value);
		co_return PaxosAcceptQuorum{ true, highestPromised };
	}
	co_return PaxosAcceptQuorum{ false, highestPromised };
}

Future<bool> paxosObservePreemption(std::vector<PaxosAcceptorInterface> const& acceptors,
                                    Key key,
                                    uint64_t instance,
                                    PaxosBallot ballot,
                                    double timeout) {
	ASSERT(!acceptors.empty());
	int const quorumSize = acceptors.size() / 2 + 1;
	int const rejectionThreshold = acceptors.size() - quorumSize + 1;
	std::vector<Future<Void>> preempted;
	for (auto const& acceptor : acceptors) {
		Future<PaxosPrepareReply> reply = sendPrepare(acceptor, PaxosPrepareRequest(key, instance, ballot, true));
		preempted.push_back(prepareShowsPreemption(reply, ballot));
	}

	auto result = co_await race(quorum(preempted, rejectionThreshold), delay(timeout));
	co_return result.index() == 0;
}

TEST_CASE("/fdbserver/core/SingleDecreePaxos/acceptor") {
	PaxosAcceptorState state;
	UID p1(1, 1);
	UID p2(2, 2);
	PaxosBallot b1(1, p1);
	PaxosBallot b2(2, p2);

	auto prepare1 = paxosPrepare(state, b1);
	ASSERT(prepare1.reply.promised);
	ASSERT(prepare1.stateChanged);
	ASSERT(state.promised == b1);

	auto accept1 = paxosAccept(state, b1, "one"_sr);
	ASSERT(accept1.reply.accepted);
	ASSERT(accept1.stateChanged);
	ASSERT(state.accepted.get().value == "one"_sr);

	auto prepare2 = paxosPrepare(state, b2);
	ASSERT(prepare2.reply.promised);
	ASSERT(prepare2.reply.accepted.get().value == "one"_sr);
	ASSERT(!paxosAccept(state, b1, "one"_sr).reply.accepted);

	auto accept2 = paxosAccept(state, b2, "one"_sr);
	ASSERT(accept2.reply.accepted);
	ASSERT(state.accepted.get().ballot == b2);
	co_return;
}

TEST_CASE("/fdbserver/core/SingleDecreePaxos/observe") {
	PaxosAcceptorState state;
	PaxosBallot ballot(3, UID(1, 1));
	ASSERT(paxosPrepare(state, ballot).reply.promised);
	auto observe = paxosPrepare(state, PaxosBallot(2, UID(2, 2)), true);
	ASSERT(!observe.reply.promised);
	ASSERT(!observe.stateChanged);
	ASSERT(state.promised == ballot);
	co_return;
}
