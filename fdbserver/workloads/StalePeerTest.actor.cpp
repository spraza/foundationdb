/*
 * StalePeerTest.actor.cpp
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

// Test that verifies stale peer references are cleaned up after a TLog
// process is killed. This reproduces the issue seen in Kubernetes environments
// where pod deletion causes a process to get a new IP, but old peer connections
// to the dead IP persist indefinitely.

#include "fdbserver/tester/workloads.h"
#include "fdbserver/core/ServerDBInfo.h"
#include "fdbrpc/SimulatorProcessInfo.h"
#include "fdbrpc/simulator.h"
#include "fdbrpc/FlowTransport.h"
#include "flow/actorcompiler.h" // must be last include

struct StalePeerTestWorkload : TestWorkload {
	static constexpr auto NAME = "StalePeerTest";

	int processesToKill;
	double waitAfterKill;
	double waitBetweenKills;
	bool testPassed;

	// Recorded old addresses of killed processes
	std::vector<NetworkAddress> oldKillAddresses;

	StalePeerTestWorkload(WorkloadContext const& wcx) : TestWorkload(wcx), testPassed(true) {
		processesToKill = getOption(options, "processesToKill"_sr, 3);
		waitAfterKill = getOption(options, "waitAfterKill"_sr, 60.0);
		waitBetweenKills = getOption(options, "waitBetweenKills"_sr, 15.0);
	}

	Future<Void> setup(Database const& cx) override { return Void(); }

	Future<Void> start(Database const& cx) override {
		if (clientId != 0)
			return Void();
		return _start(this, cx);
	}

	void disableFailureInjectionWorkloads(std::set<std::string>& out) const override { out.insert("all"); }

	// Find processes currently serving as TLogs by looking at LogSystemConfig
	std::vector<NetworkAddress> findCurrentTLogAddresses() {
		std::vector<NetworkAddress> result;
		const auto& logConfig = dbInfo->get().logSystemConfig;
		for (const auto& tlogset : logConfig.tLogs) {
			if (!tlogset.isLocal)
				continue;
			for (const auto& log : tlogset.tLogs) {
				if (log.present()) {
					result.push_back(log.interf().address());
				}
			}
		}
		return result;
	}

	static void printNonZeroPeerReferencesHelper(ISimulator::ProcessInfo* proc, const char* context) {
		auto* transport = static_cast<FlowTransport*>((void*)proc->global(INetwork::enFlowTransport));
		if (!transport)
			return;
		auto& allPeers = transport->getAllPeers();
		for (const auto& peer : allPeers) {
			if (peer.second->peerReferences > 0) {
				TraceEvent("StalePeerTestNonZeroPeerReference")
				    .detail("Context", context)
				    .detail("CheckedProcess", proc->address)
				    .detail("ProcessName", proc->name)
				    .detail("PeerAddress", peer.first.toString())
				    .detail("PeerReferences", peer.second->peerReferences)
				    .detail("PeerReliableEmpty", peer.second->reliable.empty())
				    .detail("PeerUnsentEmpty", peer.second->unsent.empty())
				    .detail("PeerOutstandingReplies", peer.second->outstandingReplies);
			}
		}
	}

	static void printNonZeroPeerReferences() {
		auto allProcesses = g_simulator->getAllProcesses();
		for (auto* proc : allProcesses) {
			if (proc->failed || proc->rebooting)
				continue;
			printNonZeroPeerReferencesHelper(proc, "AllProcesses");
		}
	}

	static void printInterfaceTracker(const std::vector<NetworkAddress>& deadAddresses) {
		auto allProcesses = g_simulator->getAllProcesses();
		for (auto* proc : allProcesses) {
			if (proc->failed || proc->rebooting)
				continue;
			auto* transport =
			    static_cast<FlowTransport*>((void*)proc->global(INetwork::enFlowTransport));
			if (!transport)
				continue;
			for (const auto& deadAddr : deadAddresses) {
				auto it = transport->interfaceTracker.map.find(deadAddr);
				if (it != transport->interfaceTracker.map.end()) {
					auto& e = it->second;
					TraceEvent("InterfaceTrackerDump")
					    .detail("CheckedProcess", proc->address)
					    .detail("DeadAddress", deadAddr)
					    .detail("Role", e.role)
					    .detail("NumCreated", e.numCreated)
					    .detail("NumDeleted", e.numDeleted)
					    .detail("Delta", e.numCreated - e.numDeleted);
				}
			}
		}
	}

	ACTOR static Future<Void> _start(StalePeerTestWorkload* self, Database cx) {
		// Wait for cluster to stabilize and reach FULLY_RECOVERED
		wait(delay(10.0));
		while (self->dbInfo->get().recoveryState < RecoveryState::FULLY_RECOVERED) {
			wait(self->dbInfo->onChange());
		}

		state std::vector<NetworkAddress> tlogAddresses = self->findCurrentTLogAddresses();

		if (tlogAddresses.empty()) {
			TraceEvent(SevWarn, "StalePeerTestNoTLogs")
			    .detail("Skipping", "No TLog addresses found in LogSystemConfig");
			return Void();
		}

		state int killCount = std::min(self->processesToKill, (int)tlogAddresses.size());
		TraceEvent("StalePeerTestStarting")
		    .detail("TLogAddressesFound", tlogAddresses.size())
		    .detail("ProcessesToKill", killCount);

		// Kill TLog processes one at a time
		state int i = 0;
		for (; i < killCount; i++) {
			state NetworkAddress oldAddr = tlogAddresses[i];

			// Find the process info for this address
			ISimulator::ProcessInfo* proc = g_simulator->getProcessByAddress(oldAddr);
			if (!proc || proc->failed) {
				TraceEvent(SevWarn, "StalePeerTestProcessNotFound").detail("Round", i).detail("Address", oldAddr);
				continue;
			}

			state Optional<Standalone<StringRef>> zoneId = proc->locality.zoneId();
			self->oldKillAddresses.push_back(oldAddr);

			TraceEvent("StalePeerTestKilling")
			    .detail("Round", i)
			    .detail("OldAddress", oldAddr)
			    .detail("ProcessClass", proc->startingClass.toString())
			    .detail("Zone", zoneId);

			// Kill the process permanently — KillInstantly marks it as failed
			// and it will NOT reboot. This simulates a k8s pod deletion where
			// the old IP becomes permanently unreachable.
			g_simulator->killProcess(proc, ISimulator::KillType::KillInstantly);

			// Wait for recovery
			wait(delay(self->waitBetweenKills));

			TraceEvent("StalePeerTestKillDone").detail("Round", i).detail("OldAddress", oldAddr);
		}

		// Wait for all recoveries to complete and stale peers to be cleaned up
		TraceEvent("StalePeerTestWaitingForCleanup1").detail("WaitSeconds", self->waitAfterKill);
		while (self->dbInfo->get().recoveryState < RecoveryState::FULLY_RECOVERED) {
			TraceEvent("StalePeerTestWaitingForCleanup2");
			wait(self->dbInfo->onChange());
		}
		TraceEvent("StalePeerTestWaitingForCleanup3").detail("WaitSeconds", self->waitAfterKill);
		wait(delay(self->waitAfterKill));
		TraceEvent("StalePeerTestWaitingForCleanup4");

		printNonZeroPeerReferences();
		printInterfaceTracker(self->oldKillAddresses);

		// Check for stale peers across ALL processes in the simulation.
		// With KillInstantly, the process is permanently dead and never reboots.
		// All peer references to the old address should be cleaned up.
		auto allProcesses = g_simulator->getAllProcesses();
		for (auto* proc : allProcesses) {
			if (proc->failed || proc->rebooting)
				continue;
			auto* transport = static_cast<FlowTransport*>((void*)proc->global(INetwork::enFlowTransport));
			if (!transport)
				continue;
			auto& allPeers = transport->getAllPeers();
			for (const auto& oldKillAddr : self->oldKillAddresses) {
				auto it = allPeers.find(oldKillAddr);
				if (it != allPeers.end() && it->second->peerReferences > 0) {
					TraceEvent(SevError, "StalePeerTestFailed")
					    .detail("CheckedProcess", proc->address)
					    .detail("ProcessName", proc->name)
					    .detail("OldAddress", oldKillAddr)
					    .detail("PeerReferences", it->second->peerReferences)
					    .detail("Connected", it->second->connected);
					self->testPassed = false;
				} else if (it != allPeers.end()) {
					TraceEvent("StalePeerTestPeerExists")
					    .detail("CheckedProcess", proc->address)
					    .detail("OldAddress", oldKillAddr)
					    .detail("PeerReferences", it->second->peerReferences)
					    .detail("Note", "Peer exists but refs <= 0, will be cleaned up");
				}
			}
		}

		// Debug: restart a random commit proxy that presumably has stale peers
		// to the dead tlog(s), then check its peer info post-restart.
		{
			auto& commitProxies = self->dbInfo->get().client.commitProxies;
			if (!commitProxies.empty()) {
				auto& chosenProxy = commitProxies[deterministicRandom()->randomInt(0, commitProxies.size())];
				state NetworkAddress proxyAddr = chosenProxy.address();
				ISimulator::ProcessInfo* proxyProc = g_simulator->getProcessByAddress(proxyAddr);

				if (proxyProc && !proxyProc->failed) {
					TraceEvent("StalePeerTestRestartingCommitProxy")
					    .detail("ProxyAddress", proxyAddr)
					    .detail("ProcessName", proxyProc->name);

					printNonZeroPeerReferencesHelper(proxyProc, "PreRestartCommitProxy");

					g_simulator->rebootProcess(proxyProc, ISimulator::KillType::Reboot);
					wait(delay(30.0));

					ISimulator::ProcessInfo* newProxyProc = g_simulator->getProcessByAddress(proxyAddr);
					if (newProxyProc && !newProxyProc->failed) {
						TraceEvent("StalePeerTestCommitProxyRestarted")
						    .detail("ProxyAddress", proxyAddr)
						    .detail("ProcessName", newProxyProc->name);
						printNonZeroPeerReferencesHelper(newProxyProc, "PostRestartCommitProxy");
					} else {
						TraceEvent(SevWarn, "StalePeerTestCommitProxyNotFoundAfterRestart")
						    .detail("ProxyAddress", proxyAddr);
					}
				}
			}
		}

		return Void();
	}

	Future<bool> check(Database const& cx) override {
		if (clientId != 0)
			return true;
		if (oldKillAddresses.empty()) {
			TraceEvent(SevWarn, "StalePeerTestSkipped").detail("Reason", "No processes were killed");
			return true;
		}
		TraceEvent("StalePeerTestResult").detail("Passed", testPassed);
		return testPassed;
	}

	void getMetrics(std::vector<PerfMetric>& m) override {}
};

WorkloadFactory<StalePeerTestWorkload> StalePeerTestWorkloadFactory;
