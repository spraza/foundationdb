// TODO (praza): remove unused headers
#include <algorithm>
#include <cstdint>
#include "fdbclient/DatabaseContext.h"
#include "fdbclient/FDBTypes.h"
#include "fdbclient/Status.h"
#include "fdbclient/StatusClient.h"
#include "fdbrpc/PerfMetric.h"
#include "fdbrpc/SimulatorProcessInfo.h"
#include "fdbrpc/simulator.h"
#include "fdbserver/Knobs.h"
#include "fdbserver/ServerDBInfo.actor.h"
#include "fdbserver/workloads/workloads.actor.h"
#include "flow/Buggify.h"
#include "flow/Error.h"
#include "flow/IPAddress.h"
#include "flow/IRandom.h"
#include "flow/NetworkAddress.h"
#include "flow/Optional.h"
#include "flow/Trace.h"
#include "flow/flow.h"
#include "flow/genericactors.actor.h"

#include "flow/actorcompiler.h" // This must be the last #include.

struct TxnTimeout : TestWorkload {
	static constexpr auto NAME = "TxnTimeout";

	bool enabled{ false };
	double testDuration{ 0.0 };

	TxnTimeout(const WorkloadContext& wctx) : TestWorkload(wctx) {
		enabled =
		    (clientId == 0); // only run this workload for a single client, and that too the first client (by its id)
		testDuration = getOption(options, "testDuration"_sr, 120);
	}

	Future<Void> setup(const Database& db) override { return Void(); }

	Future<Void> start(const Database& db) override {
		if (!g_network->isSimulated() || !enabled) {
			return Void();
		}
		return timeout(reportErrors(workload(this, db), "TxnTimeoutError"), testDuration, Void());
	}

	Future<bool> check(const Database& db) override {
		TraceEvent("TxnTimeoutCheckStart");
		TraceEvent("TxnTimeoutCheckEnd");
		return true;
	}

	void getMetrics(std::vector<PerfMetric>& m) override {}

	ACTOR Future<Void> workload(TxnTimeout* self, Database db) {
		TraceEvent("TxnTimeoutWorkloadStart");
		wait(delay(self->testDuration * 0.8));
		TraceEvent("TxnTimeoutWorkloadEnd");
		return Void();
	}
};

WorkloadFactory<TxnTimeout> TxnTimeoutWorkloadFactory;
