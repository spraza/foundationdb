/*
 * StuckFailback.actor.cpp
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

#include "fdbclient/GenericManagementAPI.actor.h"
#include "fdbclient/ManagementAPI.actor.h"
#include "fdbclient/NativeAPI.actor.h"
#include "fdbserver/RecoveryState.h"
#include "fdbserver/ServerDBInfo.actor.h"
#include "fdbserver/workloads/workloads.actor.h"

#include "flow/Trace.h"
#include "flow/actorcompiler.h" // This must be the last #include.

struct StuckFailbackWorkload : TestWorkload {
	static constexpr auto NAME = "StuckFailback";
	bool enabled;
	double testDuration;
	bool testSuccess;

	StuckFailbackWorkload(WorkloadContext const& wcx) : TestWorkload(wcx) {
		enabled =
		    !clientId && g_network->isSimulated(); // only do this on the "first" client, and only when in simulation
		testDuration = getOption(options, "testDuration"_sr, 300.0);
		testSuccess = false;
		g_simulator->usableRegions = 2; // is this needed? similar code in toml file
	}

	Future<Void> setup(Database const& cx) override { return Void(); }

	Future<Void> start(Database const& cx) override {
		if (!enabled) {
			return Void();
		}
		return timeout(reportErrors(client(this, cx), "StuckFailbackTimedOut"), testDuration, Void());
	}

	Future<bool> check(Database const& cx) override {
		if (!enabled) {
			return true;
		}
		if (!testSuccess) {
			TraceEvent(SevError, "StuckFailbackWorkloadFailed");
		}
		return testSuccess;
	}

	ACTOR static Future<Void> originalDbConfig(StuckFailbackWorkload* self, Database cx) {
		// At the start of your workload, enable both regions:
		TraceEvent("StuckFailbackWorkload_OriginalBegin");
		wait(success(ManagementAPI::changeConfig(cx.getReference(), g_simulator->originalRegions, true)));
		TraceEvent("StuckFailbackWorkload_OriginalChanged");
		wait(waitForFullReplication(cx)); // Make sure both regions are ready
		TraceEvent("StuckFailbackWorkload_OriginalReplicated");
		return Void();
	}

	ACTOR static Future<Void> doFailover(StuckFailbackWorkload* self, Database cx) {
		TraceEvent("StuckFailbackWorkload_FailoverVerify");

		wait(waitForPrimaryDC(cx, "0"_sr));

		TraceEvent("StuckFailbackWorkload_FailoverBegin");

		wait(success(ManagementAPI::changeConfig(cx.getReference(), g_simulator->disablePrimary, true)));

		TraceEvent("StuckFailbackWorkload_FailoverWait");

		wait(waitForPrimaryDC(cx, "1"_sr));

		TraceEvent("StuckFailbackWorkload_FailoverComplete");

		return Void();
	}

	ACTOR static Future<Void> doFailback(StuckFailbackWorkload* self, Database cx) {
		TraceEvent("StuckFailbackWorkload_FailbackVerify");

		wait(waitForPrimaryDC(cx, "1"_sr));

		TraceEvent("StuckFailbackWorkload_FailbackBegin");

		wait(success(ManagementAPI::changeConfig(cx.getReference(), g_simulator->originalRegions, true)));
		TraceEvent("StuckFailbackWorkload_FailbackWait");

		wait(waitForPrimaryDC(cx, "0"_sr));

		TraceEvent("StuckFailbackWorkload_FailbackComplete");
		return Void();
	}

	ACTOR Future<Void> client(StuckFailbackWorkload* self, Database cx) {
		TraceEvent("StuckFailbackWorkload_ClientBegin");
		while (self->dbInfo->get().recoveryState < RecoveryState::FULLY_RECOVERED) {
			TraceEvent("StuckFailbackWorkload_ClientNotFullyRecovered");
			wait(self->dbInfo->onChange());
		}
		TraceEvent("StuckFailbackWorkload_ClientFullyRecovered");

		wait(self->originalDbConfig(self, cx));
		wait(self->doFailover(self, cx));
		wait(self->doFailback(self, cx));

		TraceEvent("StuckFailbackWorkload_ClientEnd");

		self->testSuccess = true;

		return Void();
	}

	void getMetrics(std::vector<PerfMetric>& m) override {}
};

WorkloadFactory<StuckFailbackWorkload> StuckFailbackWorkloadFactory;
