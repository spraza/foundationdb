/*
 * SingleDecreePaxos.h
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

#ifndef FDBSERVER_SINGLE_DECREE_PAXOS_H
#define FDBSERVER_SINGLE_DECREE_PAXOS_H
#pragma once

#include "fdbclient/FDBTypes.h"
#include "fdbrpc/fdbrpc.h"
#include "flow/Hostname.h"

struct PaxosBallot {
	constexpr static FileIdentifier file_identifier = 13094001;

	uint64_t round{ 0 };
	UID proposer;

	PaxosBallot() = default;
	PaxosBallot(uint64_t round, UID proposer) : round(round), proposer(proposer) {}

	bool operator<(PaxosBallot const& rhs) const {
		return round < rhs.round || (round == rhs.round && proposer < rhs.proposer);
	}
	bool operator>(PaxosBallot const& rhs) const { return rhs < *this; }
	bool operator<=(PaxosBallot const& rhs) const { return !(*this > rhs); }
	bool operator>=(PaxosBallot const& rhs) const { return !(*this < rhs); }
	bool operator==(PaxosBallot const& rhs) const { return round == rhs.round && proposer == rhs.proposer; }
	bool operator!=(PaxosBallot const& rhs) const { return !(*this == rhs); }

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, round, proposer);
	}
};

struct PaxosAcceptedValue {
	constexpr static FileIdentifier file_identifier = 13094002;

	PaxosBallot ballot;
	Value value;

	PaxosAcceptedValue() = default;
	PaxosAcceptedValue(PaxosBallot ballot, Value value) : ballot(ballot), value(std::move(value)) {}

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, ballot, value);
	}
};

struct PaxosAcceptorState {
	constexpr static FileIdentifier file_identifier = 13094003;

	PaxosBallot promised;
	Optional<PaxosAcceptedValue> accepted;

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, promised, accepted);
	}
};

struct PaxosPrepareReply {
	constexpr static FileIdentifier file_identifier = 13094004;

	bool promised{ false };
	PaxosBallot promisedBallot;
	Optional<PaxosAcceptedValue> accepted;

	PaxosPrepareReply() = default;
	PaxosPrepareReply(bool promised, PaxosAcceptorState const& state)
	  : promised(promised), promisedBallot(state.promised), accepted(state.accepted) {}

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, promised, promisedBallot, accepted);
	}
};

struct PaxosPrepareRequest {
	constexpr static FileIdentifier file_identifier = 13094005;

	Key key;
	uint64_t instance{ 0 };
	PaxosBallot ballot;
	bool observeOnly{ false };
	ReplyPromise<PaxosPrepareReply> reply;

	PaxosPrepareRequest() = default;
	PaxosPrepareRequest(Key key, uint64_t instance, PaxosBallot ballot, bool observeOnly = false)
	  : key(std::move(key)), instance(instance), ballot(ballot), observeOnly(observeOnly) {}

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, key, instance, ballot, observeOnly, reply);
	}
};

struct PaxosAcceptReply {
	constexpr static FileIdentifier file_identifier = 13094006;

	bool accepted{ false };
	PaxosBallot promisedBallot;

	PaxosAcceptReply() = default;
	PaxosAcceptReply(bool accepted, PaxosBallot promisedBallot) : accepted(accepted), promisedBallot(promisedBallot) {}

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, accepted, promisedBallot);
	}
};

struct PaxosAcceptRequest {
	constexpr static FileIdentifier file_identifier = 13094007;

	Key key;
	uint64_t instance{ 0 };
	PaxosBallot ballot;
	Value value;
	ReplyPromise<PaxosAcceptReply> reply;

	PaxosAcceptRequest() = default;
	PaxosAcceptRequest(Key key, uint64_t instance, PaxosBallot ballot, Value value)
	  : key(std::move(key)), instance(instance), ballot(ballot), value(std::move(value)) {}

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, key, instance, ballot, value, reply);
	}
};

struct PaxosAcceptorInterface {
	RequestStream<PaxosPrepareRequest> prepare;
	RequestStream<PaxosAcceptRequest> accept;
	Optional<Hostname> hostname;

	PaxosAcceptorInterface() = default;
	explicit PaxosAcceptorInterface(NetworkAddress const& remote);
	explicit PaxosAcceptorInterface(INetwork* local);
	explicit PaxosAcceptorInterface(Hostname const& hostname) : hostname(hostname) {}
};

struct PaxosPrepareTransition {
	PaxosPrepareReply reply;
	bool stateChanged{ false };
};

struct PaxosAcceptTransition {
	PaxosAcceptReply reply;
	bool stateChanged{ false };
};

PaxosPrepareTransition paxosPrepare(PaxosAcceptorState& state, PaxosBallot ballot, bool observeOnly = false);
PaxosAcceptTransition paxosAccept(PaxosAcceptorState& state, PaxosBallot ballot, Value value);

struct PaxosPrepareQuorum {
	PaxosBallot ballot;
	Optional<PaxosAcceptedValue> accepted;
	uint64_t highestObservedRound{ 0 };
};

struct PaxosAcceptQuorum {
	bool chosen{ false };
	PaxosBallot highestPromised;
};

Future<PaxosPrepareQuorum> paxosPrepareQuorum(std::vector<PaxosAcceptorInterface> const& acceptors,
                                              Key key,
                                              uint64_t instance,
                                              UID proposer,
                                              uint64_t minimumRound = 1);

Future<PaxosAcceptQuorum> paxosAcceptQuorum(std::vector<PaxosAcceptorInterface> const& acceptors,
                                            Key key,
                                            uint64_t instance,
                                            PaxosBallot ballot,
                                            Value value,
                                            bool requireAll = false);

Future<bool> paxosObservePreemption(std::vector<PaxosAcceptorInterface> const& acceptors,
                                    Key key,
                                    uint64_t instance,
                                    PaxosBallot ballot,
                                    double timeout);

#endif
