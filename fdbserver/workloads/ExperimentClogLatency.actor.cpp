#include "fdbclient/Status.h"
#include "fdbclient/StatusClient.h"
#include "fdbrpc/SimulatorProcessInfo.h"
#include "fdbserver/ServerDBInfo.actor.h"
#include "fdbserver/workloads/workloads.actor.h"
#include "flow/Error.h"
#include "flow/IPAddress.h"
#include "flow/NetworkAddress.h"
#include "flow/actorcompiler.h" // This must be the last #include.
#include <thread>
#include <chrono>

struct ExperimentClogLatency : TestWorkload {
	static constexpr auto NAME = "ExperimentClogLatency";
	double testDuration{ 0 };
	bool enabled{ false };

	ExperimentClogLatency(const WorkloadContext& wctx) : TestWorkload(wctx) {
		enabled = (clientId == 0);
		testDuration = getOption(options, "testDuration"_sr, 0);
	}

	// Overrides
	Future<Void> setup(Database const& cx) override { return Void(); }
	Future<Void> start(Database const& cx) override {
		if (g_network->isSimulated() && enabled) {
			return timeout(reportErrors(workload(this, cx), "ExperimentClogLatencyError"), testDuration, Void());
		} else {
			return Void();
		}
	}
	Future<bool> check(Database const& cx) override { return true; }
	void getMetrics(std::vector<PerfMetric>& m) override {}

	// Workload
	static NetworkAddress getRandomPrimaryTLog(ExperimentClogLatency* self) {
		IPAddress cc = self->dbInfo->get().clusterInterface.address().ip;
		for (int i = 0; i < self->dbInfo->get().logSystemConfig.tLogs.size(); i++) {
			const auto& tlogset = self->dbInfo->get().logSystemConfig.tLogs[i];
			if (!tlogset.isLocal)
				continue;
			for (const auto& log : tlogset.tLogs) {
				const NetworkAddress& addr = log.interf().address();
				if (cc != addr.ip) {
					return addr;
				}
			}
		}
		UNREACHABLE();
	}

	ACTOR static Future<NetworkAddress> getRandomPrimaryTLog(Database db) {
		StatusObject status = wait(StatusClient::statusFetcher(db));
		StatusObjectReader reader(status);
		StatusObjectReader cluster;
		StatusObjectReader processMap;
		if (!reader.get("cluster", cluster)) {
			TraceEvent("NoCluster");
			ASSERT(false);
		}
		if (!cluster.get("processes", processMap)) {
			TraceEvent("NoProcesses");
			ASSERT(false);
		}
		for (auto p : processMap.obj()) {
			StatusObjectReader process(p.second);
			ASSERT(process.has("roles"));
			StatusArray roles = p.second.get_obj()["roles"].get_array();
			if (roles.size() == 1) { // ensure process has only 1 role
				StatusObjectReader role = roles[0];
				if (role["role"].get_str() == "log") { // ensure that 1 role is log
					// get machine id, on that machine, ensure we are the only process running
					// auto locality = p.second.get_obj()["locality"];
					// auto localityObj = locality.
					// std::string machineId = localityObj["machineid"].get_str();
					// std::string machineId;
					// process.get("locality.machineid", machineId);
					// std::cout << "selected tlog process is on machine " << machineId << std::endl;
					// return p.second.get_obj()["address"].get_str();
					// return p.first;
					// std::cout << "picked tlog with process id " << p.first << " and address (ip + port) "
					//           << p.second.get_obj()["address"].get_str() << std::endl;
					return NetworkAddress::parse(p.second.get_obj()["address"].get_str());
				}
			}
		}
		std::cout << "Could not find any valid TLog\n";
		ASSERT(false);
		return NetworkAddress();
	}

	ACTOR static Future<Void> printStatusJson(Database db) {
		StatusObject status = wait(StatusClient::statusFetcher(db));
		StatusObjectReader reader(status);
		printf("%s\n",
		       json_spirit::write_string(json_spirit::mValue(reader.obj()), json_spirit::Output_options::pretty_print)
		           .c_str());
		return Void();
	}

	ACTOR static Future<std::vector<IPAddress>> getPrimarySSIPs(Database db) {
		state std::vector<IPAddress> ret;
		Transaction tr(db);
		tr.setOption(FDBTransactionOptions::READ_SYSTEM_KEYS);
		tr.setOption(FDBTransactionOptions::PRIORITY_SYSTEM_IMMEDIATE);
		tr.setOption(FDBTransactionOptions::LOCK_AWARE);
		std::vector<std::pair<StorageServerInterface, ProcessClass>> results =
		    wait(NativeAPI::getServerListAndProcessClasses(&tr));
		for (auto& [ssi, p] : results) {
			if (p == ProcessClass::TesterClass) {
				continue;
			}
			if (ssi.locality.dcId().present() && ssi.locality.dcId().get() == g_simulator->primaryDcId) {
				ret.push_back(ssi.address().ip);
			}
		}
		return ret;
	}

	ACTOR static Future<Void> measureMaxSSLagSec(ExperimentClogLatency* self, Database db) {
		try {
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
			double maxSSLagSec{ -1 };
			for (auto p : processMap.obj()) {
				StatusObjectReader process(p.second);
				if (process.has("roles")) {
					StatusArray roles = p.second.get_obj()["roles"].get_array();
					for (StatusObjectReader role : roles) {
						ASSERT(role.has("role"));
						if (role.has("data_lag")) {
							ASSERT(role["role"].get_str() == "storage");
							auto dataLag = role["data_lag"].get_obj();
							ASSERT(dataLag.contains("seconds"));
							ASSERT(dataLag.contains("versions"));
							TraceEvent("SSDataLag")
							    .detail("Process", p.first)
							    .detail("Role", role["role"].get_str())
							    .detail("SecondLag", dataLag["seconds"].get_value<double>())
							    .detail("VersionLag", dataLag["versions"].get_int64());
							maxSSLagSec = std::max(maxSSLagSec, dataLag["seconds"].get_value<double>());
						}
					}
				}
			}
			std::cout << "maxSSLag = " << maxSSLagSec << std::endl;
		} catch (Error& e) {
			std::cout << "measure error, " << e.name() << ", " << e.code() << ", " << e.what() << std::endl;
			ASSERT(false);
		}
		// TODO (praza): Uncomment this assert when gray failure detection is improved to automatically fix this issue
		// ASSERT(maxSSLagSec < self->lagThresholdSec);
		return Void();
	}

	ACTOR static Future<Void> doClog(ExperimentClogLatency* self, Database db) {
		wait(delay(40.0));
		std::cout << "ready to clog\n";

		// NetworkAddress tlog = wait(getRandomPrimaryTLog(db));
		state NetworkAddress tlog;
		try {
			tlog = getRandomPrimaryTLog(self);
		} catch (Error& e) {
			std::cout << "get tlog error: " << e.name() << ", " << e.code() << ", " << e.what() << std::endl;
			ASSERT(false);
		}
		state IPAddress tlogIP = tlog.ip;
		// state IPAddress tlogIP = IPAddress::parse("abcd::2:2:1:3").get();

		state IPAddress cc = self->dbInfo->get().clusterInterface.address().ip;

		state std::vector<IPAddress> primarySSIps;
		try {
			std::vector<IPAddress> x = wait(getPrimarySSIPs(db));
			primarySSIps = x;
		} catch (Error& e) {
			std::cout << "get ss error: " << e.name() << ", " << e.code() << ", " << e.what() << std::endl;
			ASSERT(false);
		}
		ASSERT(!primarySSIps.empty());

		for (const auto& ip : primarySSIps) {
			if (tlogIP == ip) {
				continue;
			}
			if (ip == cc) {
				continue;
			}
			// std::cout << "process name: " << process->name << std::endl;
			g_simulator->clogPair(tlogIP, ip, self->testDuration);
			// g_simulator->clogPair(ip, tlogIP, self->testDuration);
			std::cout << "bidirectional clog down between tlog = " << tlogIP.toString()
			          << " and non-cc SS process = " << ip.toString() << std::endl;
			break;
		}

		std::cout << "clogging done, waiting for test to finish via timeout\n";
		wait(Never());

		ASSERT(false);
		return Void();
	}

	ACTOR Future<Void> workload(ExperimentClogLatency* self, Database db) {
		while (self->dbInfo->get().recoveryState < RecoveryState::FULLY_RECOVERED) {
			wait(self->dbInfo->onChange());
		}

		wait(delay(5.0));

		wait(printStatusJson(db));

		state Future<Void> clogFuture = doClog(self, db);

		loop choose {
			when(wait(clogFuture)) {}
			when(wait(delay(5))) {
				wait(measureMaxSSLagSec(self, db));
			}
		}
	}
};

WorkloadFactory<ExperimentClogLatency> ExperimentClogLatencyFactory;
