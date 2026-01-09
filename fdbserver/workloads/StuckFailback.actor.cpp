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
#include "fdbclient/StatusClient.h"
#include "fdbserver/MasterInterface.h"
#include "fdbserver/RecoveryState.h"
#include "fdbserver/ServerDBInfo.actor.h"
#include "fdbserver/workloads/workloads.actor.h"

#include "flow/Trace.h"
#include "flow/actorcompiler.h" // This must be the last #include.

struct StuckFailbackWorkload : TestWorkload {
	static constexpr auto NAME = "StuckFailback";
	bool enabled;
	double testDuration;
	bool testFinished;
	double latestDcLagSeconds;
	DBRecoveryCount postFailbackRecoveryCount;
	DBRecoveryCount latestFailbackRecoveryCount;
	RecoveryState latestRecoveryState;

	StuckFailbackWorkload(WorkloadContext const& wcx) : TestWorkload(wcx) {
		enabled =
		    !clientId && g_network->isSimulated(); // only do this on the "first" client, and only when in simulation
		testDuration = getOption(options, "testDuration"_sr, 300.0);
		testFinished = false;
		latestDcLagSeconds = 0;
		postFailbackRecoveryCount = 0;
		latestFailbackRecoveryCount = 0;
		latestRecoveryState = RecoveryState::UNINITIALIZED;
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
		if (isGeneralBuggifyEnabled()) { // with buggify, we can sometimes fail? focus on buggify=off for now
			return true;
		}
		if (!enabled) {
			return true;
		}
		// recoveryState = fully_recovered
		// recovery after failback = 1 (just failback one)
		if (!testFinished) {
			TraceEvent(SevError, "StuckFailbackWorkloadFailedUnfinished")
			    .detail("LatestDcLagSeconds", latestDcLagSeconds)
			    .detail("PostFailbackRecoveryCount", postFailbackRecoveryCount)
			    .detail("LatestFailbackRecoveryCount", latestFailbackRecoveryCount)
			    .detail("LatestRecoveryState", latestRecoveryState);
			return false;
		}
		if (latestDcLagSeconds > 1) {
			TraceEvent(SevError, "StuckFailbackWorkloadFailedHighDCLag")
			    .detail("LatestDcLagSeconds", latestDcLagSeconds)
			    .detail("PostFailbackRecoveryCount", postFailbackRecoveryCount)
			    .detail("LatestFailbackRecoveryCount", latestFailbackRecoveryCount)
			    .detail("LatestRecoveryState", latestRecoveryState);
			return false;
		}
		if (latestFailbackRecoveryCount - postFailbackRecoveryCount > 2) {
			TraceEvent(SevError, "StuckFailbackWorkloadFailedTooManyRecoveries")
			    .detail("LatestDcLagSeconds", latestDcLagSeconds)
			    .detail("PostFailbackRecoveryCount", postFailbackRecoveryCount)
			    .detail("LatestFailbackRecoveryCount", latestFailbackRecoveryCount)
			    .detail("LatestRecoveryState", latestRecoveryState);
			return false;
		}
		if (latestRecoveryState != RecoveryState::FULLY_RECOVERED) {
			TraceEvent(SevError, "StuckFailbackWorkloadFailedClusterNotFullyRecovered")
			    .detail("LatestDcLagSeconds", latestDcLagSeconds)
			    .detail("PostFailbackRecoveryCount", postFailbackRecoveryCount)
			    .detail("LatestFailbackRecoveryCount", latestFailbackRecoveryCount)
			    .detail("LatestRecoveryState", latestRecoveryState);
			return false;
		}
		TraceEvent("StuckFailbackWorkloadPassed")
		    .detail("LatestDcLagSeconds", latestDcLagSeconds)
		    .detail("PostFailbackRecoveryCount", postFailbackRecoveryCount)
		    .detail("LatestFailbackRecoveryCount", latestFailbackRecoveryCount)
		    .detail("LatestRecoveryState", latestRecoveryState);
		return true;
	}

	void disableFailureInjectionWorkloads(std::set<std::string>& out) const override { out.insert("all"); }

	// Fetches details (versions and seconds) of the specified type of lag (tlog/storage server/data center lag) from
	// the given status json document.
	bool fetchLagFromStatusObject(std::string path, StatusObjectReader& statusObj, Version& versions, double& seconds) {
		StatusObjectReader lagObject;
		if (!statusObj.get(path, lagObject)) {
			return false;
		}

		if (!lagObject.get("versions", versions)) {
			return false;
		}

		if (!lagObject.get("seconds", seconds)) {
			return false;
		}

		return true;
	}

	ACTOR static Future<Void> reportDCLag(StuckFailbackWorkload* self, Database cx) {
		loop {
			wait(delay(15));
			TraceEvent("StuckFailbackWorkload_ReportDCLagStartAttempt");

			StatusObject result = wait(StatusClient::statusFetcher(cx));
			StatusObjectReader statusObj(result);
			StatusObjectReader statusObjCluster;
			if (!statusObj.get("cluster", statusObjCluster)) {
				TraceEvent("StuckFailbackWorkload_ReportDCLagNoCluster");
				continue;
			}

			// Fetch the lag between primary and remote data centers.
			Version dcLagInVersions = 0;
			double dcLagInSeconds = 0;
			if (!self->fetchLagFromStatusObject("datacenter_lag", statusObjCluster, dcLagInVersions, dcLagInSeconds)) {
				TraceEvent("StuckFailbackWorkload_ReportDCLagNoLagData");
				continue;
			}
			(void)dcLagInVersions;
			TraceEvent("StuckFailbackWorkload_ReportDCLagData").detail("DcLagSeconds", dcLagInSeconds);
			self->latestDcLagSeconds = dcLagInSeconds;
		}
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

		state Future<Void> reportDCLagActor = reportDCLag(self, cx);
		wait(self->originalDbConfig(self, cx));
		wait(self->doFailover(self, cx));
		wait(delay(100));
		wait(self->doFailback(self, cx));
		self->postFailbackRecoveryCount = self->dbInfo->get().recoveryCount;
		wait(delay(100));

		TraceEvent("StuckFailbackWorkload_ClientEnd");

		self->latestFailbackRecoveryCount = self->dbInfo->get().recoveryCount;
		self->latestRecoveryState = self->dbInfo->get().recoveryState;
		self->testFinished = true;

		return Void();
	}

	void getMetrics(std::vector<PerfMetric>& m) override {}
};

WorkloadFactory<StuckFailbackWorkload> StuckFailbackWorkloadFactory;
