/*
 * PaxosCoordinatorCorrectness.cpp
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

#include <cstdio>

#include "fdbserver/core/CoordinationInterface.h"
#include "fdbserver/core/PaxosSequence.h"
#include "fdbserver/tester/workloads.h"

namespace {

struct PaxosCoordinatorCorrectnessWorkload : TestWorkload {
	static constexpr auto NAME = "PaxosCoordinatorCorrectness";

	bool enabled;
	int actorCount;
	int operationsPerActor;
	double testDuration;
	ServerCoordinators coordinators;
	Key key;
	int successfulWrites{ 0 };

	explicit PaxosCoordinatorCorrectnessWorkload(WorkloadContext const& wcx)
	  : TestWorkload(wcx), enabled(clientId == 0), actorCount(getOption(options, "actorCount"_sr, 4)),
	    operationsPerActor(getOption(options, "operationsPerActor"_sr, 8)),
	    testDuration(getOption(options, "testDuration"_sr, 30.0)), coordinators(wcx.ccr),
	    key(format("PaxosCoordinatorCorrectness/%016llx", wcx.sharedRandomNumber)) {}

	static void validatePrevious(PaxosSequenceReservation const& reservation) {
		if (reservation.instance == 1) {
			ASSERT(reservation.currentValue.empty());
			return;
		}

		unsigned long long encodedInstance = 0;
		int parsed = std::sscanf(reservation.currentValue.toString().c_str(), "%llu:", &encodedInstance);
		ASSERT(parsed == 1);
		ASSERT(encodedInstance + 1 == reservation.instance);
	}

	Future<Void> setup(Database const&) override {
		if (!enabled) {
			co_return;
		}

		PaxosSequenceReservation reservation =
		    co_await paxosSequenceRead(coordinators.stateServers, key, deterministicRandom()->randomUniqueID());
		validatePrevious(reservation);
		PaxosAcceptQuorum result = co_await paxosSequenceSet(
		    coordinators.stateServers, reservation, Value(format("%llu:setup", reservation.instance)));
		ASSERT(result.chosen);
		++successfulWrites;
	}

	Future<Void> writer(int actor) {
		for (int operation = 0; operation < operationsPerActor; ++operation) {
			UID proposer = deterministicRandom()->randomUniqueID();
			PaxosSequenceReservation reservation = co_await paxosSequenceRead(coordinators.stateServers, key, proposer);
			validatePrevious(reservation);
			Value proposal(
			    format("%llu:%d:%d:%s", reservation.instance, actor, operation, proposer.shortString().c_str()));
			PaxosAcceptQuorum result = co_await paxosSequenceSet(coordinators.stateServers, reservation, proposal);
			if (result.chosen) {
				++successfulWrites;
			}
		}
	}

	Future<Void> start(Database const&) override {
		if (!enabled) {
			co_return;
		}

		std::vector<Future<Void>> writers;
		writers.reserve(actorCount);
		for (int actor = 0; actor < actorCount; ++actor) {
			writers.push_back(writer(actor));
		}
		co_await (waitForAll(writers) || delay(testDuration));
	}

	Future<bool> check(Database const&) override {
		if (!enabled) {
			co_return true;
		}

		PaxosSequenceReservation reservation =
		    co_await paxosSequenceRead(coordinators.stateServers, key, deterministicRandom()->randomUniqueID());
		validatePrevious(reservation);
		ASSERT(reservation.instance >= 2);
		TraceEvent("PaxosCoordinatorCorrectnessComplete")
		    .detail("NextInstance", reservation.instance)
		    .detail("SuccessfulWrites", successfulWrites);
		co_return successfulWrites > 1;
	}

	void getMetrics(std::vector<PerfMetric>& metrics) override {
		metrics.emplace_back("Paxos writes chosen", successfulWrites, Averaged::False);
	}
};

WorkloadFactory<PaxosCoordinatorCorrectnessWorkload> PaxosCoordinatorCorrectnessWorkloadFactory;

} // namespace
