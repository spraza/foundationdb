#include "fdbclient/DatabaseContext.h"
#include "fdbclient/FDBTypes.h"
#include "fdbclient/Status.h"
#include "fdbclient/StatusClient.h"
#include "fdbrpc/PerfMetric.h"
#include "fdbrpc/SimulatorProcessInfo.h"
#include "fdbrpc/simulator.h"
#include "fdbserver/ServerDBInfo.actor.h"
#include "fdbserver/workloads/workloads.actor.h"
#include "flow/Buggify.h"

#include "flow/IPAddress.h"
#include "flow/IRandom.h"
#include "flow/Optional.h"
#include "flow/Trace.h"
#include "flow/actorcompiler.h" // This must be the last #include.
#include "flow/flow.h"
#include "flow/genericactors.actor.g.h"
#include "flow/genericactors.actor.h"
#include <cstdint>

struct ClogRemoteTLog : TestWorkload {
	static constexpr auto NAME = "ClogRemoteTLog";

	bool enabled{ false };
	double testDuration{ 0.0 };
	double lagMeasurementFrequencySec{ 0 };
	double clogInitDelaySec{ 0 };
	double clogDuration{ 0 };

	ClogRemoteTLog(const WorkloadContext& wctx) : TestWorkload(wctx) {
		enabled =
		    (clientId == 0); // only run this workload for a single client, and that too  the first client (by its id)
		testDuration = getOption(options, "testDuration"_sr, 1000);
		lagMeasurementFrequencySec = getOption(options, "lagMeasurementFrequencySec"_sr, 5);
		clogInitDelaySec = getOption(options, "clogInitDelaySec"_sr, 5);
		clogDuration = getOption(options, "clogDuration"_sr, 5);
	}

	Future<Void> setup(const Database& db) override { return Void(); }

	Future<Void> start(const Database& db) override {
		if (g_network->isSimulated() && enabled) {
			return timeout(reportErrors(workload(this, db), "ClogRemoteTLogError"), testDuration, Void());
		}
		return Void();
	}

	Future<bool> check(const Database& db) override { return true; }

	void getMetrics(std::vector<PerfMetric>& m) override {}

	ACTOR static Future<Void> fetchSSVersionLag(Database db) {
		StatusObject status = wait(StatusClient::statusFetcher(db));
		StatusObjectReader reader(status);
		StatusObjectReader cluster;
		StatusObjectReader processMap;
		if (!reader.get("cluster", cluster)) {
			TraceEvent("NoCluster");
			return Void();
		}
		if (!cluster.get("processes", processMap)) {
			TraceEvent("NoProcesses");
			return Void();
		}
		for (auto p : processMap.obj()) {
			StatusObjectReader process(p.second);
			if (process.has("roles")) {
				StatusArray roles = p.second.get_obj()["roles"].get_array();
				for (StatusObjectReader role : roles) {
					ASSERT(role.has("role"));
					if (role.has("data_lag")) {
						auto dataLag = role["data_lag"].get_obj();
						ASSERT(dataLag.contains("seconds"));
						ASSERT(dataLag.contains("versions"));
						TraceEvent("DataLag")
						    .detail("Process", p.first)
						    .detail("Role", role["role"].get_str())
						    .detail("SecondLag", dataLag["seconds"].get_value<double>())
						    .detail("VersionLag", dataLag["versions"].get_int64());
					}
				}
			}
		}
		return Void();
	}

	ACTOR static Future<Void> clogTLog(ClogRemoteTLog* self) {
		wait(delay(self->clogInitDelaySec));
		std::vector<IPAddress> remoteIPs;
		for (const auto& process : g_simulator->getAllProcesses()) {
			const auto& ip = process->address.ip;
			if (process->locality.dcId().present() && process->locality.dcId() == g_simulator->remoteDcId) {
				remoteIPs.push_back(ip);
			}
		}
		ASSERT(!remoteIPs.empty());
		std::vector<IPAddress> remoteTLogIPs;
		for (const auto& tLogSet : self->dbInfo->get().logSystemConfig.tLogs) {
			if (tLogSet.isLocal) {
				continue;
			}
			for (const auto& tLog : tLogSet.tLogs) {
				remoteTLogIPs.push_back(tLog.interf().address().ip);
			}
		}
		ASSERT(!remoteTLogIPs.empty());
		state IPAddress remoteTLogIP = remoteTLogIPs[deterministicRandom()->randomInt(0, remoteTLogIPs.size())];
		state std::vector<IPAddress> cloggedRemoteIPs;
		for (const auto& remoteIP : remoteIPs) {
			if (remoteIP != remoteTLogIP) {
				TraceEvent("ClogRemoteTLog").detail("RemoteTLogIPSrc", remoteTLogIP).detail("RemoteIPDst", remoteIP);
				g_simulator->clogPair(remoteTLogIP, remoteIP, self->testDuration);
				cloggedRemoteIPs.push_back(remoteIP);
			}
		}
		ASSERT(!cloggedRemoteIPs.empty());

		wait(delay(self->clogDuration));
		TraceEvent("UnclogRemoteTLogStart");
		for (const auto& remoteIP : cloggedRemoteIPs) {
			TraceEvent("UnclogRemoteTLog").detail("RemoteTLogIPSrc", remoteTLogIP).detail("RemoteIPDst", remoteIP);
			g_simulator->unclogPair(remoteTLogIP, remoteIP);
		}
		TraceEvent("UnclogRemoteTLogFinished");

		wait(Never());
		return Void();
	}

	ACTOR Future<Void> workload(ClogRemoteTLog* self, Database db) {
		state Future<Void> clog = self->clogTLog(self);
		loop choose {
			when(wait(delay(self->lagMeasurementFrequencySec))) {
				wait(fetchSSVersionLag(db));
			}
			when(wait(clog)) {}
		}
	}
};

WorkloadFactory<ClogRemoteTLog> ClogRemoteTlogWorkloadFactory;