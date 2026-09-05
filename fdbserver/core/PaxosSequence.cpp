/*
 * PaxosSequence.cpp
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

#include "fdbserver/core/PaxosSequence.h"

#include "flow/Trace.h"

Future<PaxosSequenceReservation> paxosSequenceRead(std::vector<PaxosAcceptorInterface> const& acceptors,
                                                   Key key,
                                                   UID proposer) {
	Value currentValue;
	uint64_t instance = 1;
	uint64_t minimumRound = 1;

	while (true) {
		PaxosPrepareQuorum prepared = co_await paxosPrepareQuorum(acceptors, key, instance, proposer, minimumRound);
		minimumRound = prepared.highestObservedRound + 1;

		if (!prepared.accepted.present()) {
			TraceEvent("PaxosSequenceReserved")
			    .detail("Key", printable(key))
			    .detail("Instance", instance)
			    .detail("BallotRound", prepared.ballot.round)
			    .detail("Proposer", prepared.ballot.proposer);
			co_return PaxosSequenceReservation{ key, currentValue, instance, prepared.ballot, instance == 1 };
		}

		PaxosAcceptedValue accepted = prepared.accepted.get();
		PaxosAcceptQuorum completed =
		    co_await paxosAcceptQuorum(acceptors, key, instance, prepared.ballot, accepted.value);
		if (!completed.chosen) {
			minimumRound = std::max(minimumRound, completed.highestPromised.round + 1);
			continue;
		}

		currentValue = accepted.value;
		TraceEvent("PaxosSequenceCompleted")
		    .detail("Key", printable(key))
		    .detail("Instance", instance)
		    .detail("BallotRound", prepared.ballot.round);
		++instance;
		minimumRound = 1;
	}
}

Future<PaxosAcceptQuorum> paxosSequenceSet(std::vector<PaxosAcceptorInterface> const& acceptors,
                                           PaxosSequenceReservation const& reservation,
                                           Value value) {
	return paxosAcceptQuorum(
	    acceptors, reservation.key, reservation.instance, reservation.ballot, std::move(value), reservation.initial);
}

Future<Void> paxosSequenceOnConflict(std::vector<PaxosAcceptorInterface> const& acceptors,
                                     PaxosSequenceReservation reservation,
                                     double pollInterval) {
	while (true) {
		if (co_await paxosObservePreemption(
		        acceptors, reservation.key, reservation.instance, reservation.ballot, pollInterval)) {
			co_return;
		}
	}
}
