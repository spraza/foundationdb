/*
 * TestTLogServer.actor.cpp
 *
 * This source file is part of the FoundationDB open source project
 *
 * Copyright 2013-2024 Apple Inc. and the FoundationDB project authors
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

#include "fdbserver/TestTLogServer.actor.h"

#include <filesystem>
#include <vector>

#include "fdbrpc/Locality.h"
#include "fdbrpc/ReplicationPolicy.h"
#include "fdbserver/TLogInterface.h"
#include "fdbserver/ServerDBInfo.actor.h"
#include "fdbserver/IDiskQueue.h"
#include "fdbserver/WorkerInterface.actor.h"
#include "fdbserver/LogSystem.h"
#include "flow/IRandom.h"

#include "flow/actorcompiler.h" // must be last include

Reference<TLogTestContext> initTLogTestContext(TestTLogOptions tLogOptions,
                                               Optional<Reference<TLogTestContext>> oldTLogTestContext) {
	Reference<TLogTestContext> context(new TLogTestContext(tLogOptions));
	context->logID = deterministicRandom()->randomUniqueID();
	context->workerID = deterministicRandom()->randomUniqueID();
	context->diskQueueBasename = tLogOptions.diskQueueBasename;
	context->numCommits = tLogOptions.numCommits;
	context->numTagsPerServer = tLogOptions.numTagsPerServer;
	context->numLogServers = tLogOptions.numLogServers;
	ASSERT(context->numLogServers == 1); // SOMEDAY: support multiple tLogs
	context->dcID = "test"_sr;
	context->tagLocality = context->primaryLocality;
	context->dbInfo = ServerDBInfo();
	if (oldTLogTestContext.present()) {
		OldTLogConf oldTLogConf;
		oldTLogConf.tLogs = oldTLogTestContext.get()->dbInfo.logSystemConfig.tLogs;
		oldTLogConf.tLogs[0].locality = oldTLogTestContext.get()->primaryLocality;
		oldTLogConf.tLogs[0].isLocal = true;
		oldTLogConf.epochBegin = oldTLogTestContext.get()->initVersion;
		oldTLogConf.epochEnd = oldTLogTestContext.get()->numCommits;
		oldTLogConf.logRouterTags = 0;
		oldTLogConf.recoverAt = oldTLogTestContext.get()->initVersion;
		oldTLogConf.epoch = oldTLogTestContext.get()->epoch;
		context->tagLocality = oldTLogTestContext.get()->primaryLocality;
		context->epoch = oldTLogTestContext.get()->epoch + 1;
		context->dbInfo.logSystemConfig.oldTLogs.push_back(oldTLogConf);
	}
	context->dbInfo.logSystemConfig.logSystemType = LogSystemType::tagPartitioned;
	context->dbInfo.logSystemConfig.recruitmentID = deterministicRandom()->randomUniqueID();
	context->initVersion = tLogOptions.initVersion;
	context->recover = tLogOptions.recover;
	context->dbInfoRef = makeReference<AsyncVar<ServerDBInfo>>(context->dbInfo);

	return context;
}

// Create and start a tLog. If optional parmeters are set, the tLog is a new generation of "tLogID"
// as described by initReq. Otherwise, it is a newborn generation 0 tLog.
ACTOR Future<Void> getTLogCreateActor(Reference<TLogTestContext> pTLogTestContext,
                                      TestTLogOptions tLogOptions,
                                      uint16_t processID,
                                      InitializeTLogRequest* initReq = nullptr,
                                      UID tLogID = UID()) {

	// build per-tLog state.
	state Reference<TLogContext> pTLogContext = pTLogTestContext->pTLogContextList[processID];
	pTLogContext->tagProcessID = processID;

	pTLogContext->tLogID = tLogID != UID(0, 0) ? tLogID : deterministicRandom()->randomUniqueID();

	TraceEvent("TestTLogServerEnterGetTLogCreateActor", pTLogContext->tLogID).detail("Epoch", pTLogTestContext->epoch);

	std::filesystem::create_directory(tLogOptions.dataFolder);

	// create persistent storage
	std::string diskQueueBasename = pTLogTestContext->diskQueueBasename + "." + pTLogContext->tLogID.toString() + "." +
	                                std::to_string(pTLogTestContext->epoch) + ".";
	state std::string diskQueueFilename = tLogOptions.dataFolder + "/" + diskQueueBasename;
	pTLogContext->persistentQueue =
	    openDiskQueue(diskQueueFilename, tLogOptions.diskQueueExtension, pTLogContext->tLogID, DiskQueueVersion::V1);

	state std::string kvStoreFilename = tLogOptions.dataFolder + "/" + tLogOptions.kvStoreFilename + "." +
	                                    pTLogContext->tLogID.toString() + "." +
	                                    std::to_string(pTLogTestContext->epoch) + ".";
	pTLogContext->persistentData = keyValueStoreMemory(kvStoreFilename,
	                                                   pTLogContext->tLogID,
	                                                   tLogOptions.kvMemoryLimit,
	                                                   tLogOptions.kvStoreExtension,
	                                                   KeyValueStoreType::MEMORY_RADIXTREE);

	// prepare tLog construction.
	Standalone<StringRef> machineID = "machine"_sr;
	LocalityData localities(
	    Optional<Standalone<StringRef>>(), pTLogTestContext->zoneID, machineID, pTLogTestContext->dcID);
	localities.set(StringRef("datacenter"_sr), pTLogTestContext->dcID);

	Reference<AsyncVar<bool>> isDegraded = FlowTransport::transport().getDegraded();
	Reference<AsyncVar<UID>> activeSharedTLog(new AsyncVar<UID>(pTLogContext->tLogID));
	Reference<AsyncVar<bool>> enablePrimaryTxnSystemHealthCheck(new AsyncVar<bool>(false));
	state PromiseStream<InitializeTLogRequest> promiseStream = PromiseStream<InitializeTLogRequest>();
	Promise<Void> oldLog;
	Promise<Void> recovery;

	// construct tLog.
	state Future<Void> tl = ::tLog(pTLogContext->persistentData,
	                               pTLogContext->persistentQueue,
	                               pTLogTestContext->dbInfoRef,
	                               localities,
	                               promiseStream,
	                               pTLogContext->tLogID,
	                               pTLogTestContext->workerID,
	                               false, /* restoreFromDisk */
	                               oldLog,
	                               recovery,
	                               pTLogTestContext->diskQueueBasename,
	                               isDegraded,
	                               activeSharedTLog,
	                               enablePrimaryTxnSystemHealthCheck);

	state InitializeTLogRequest initTLogReq = InitializeTLogRequest();
	if (initReq != nullptr) {
		initTLogReq = *initReq;
	} else {
		std::vector<Tag> tags;
		for (uint32_t tagID = 0; tagID < pTLogTestContext->numTagsPerServer; tagID++) {
			Tag tag(pTLogTestContext->tagLocality, tagID);
			tags.push_back(tag);
		}
		initTLogReq.epoch = 1;
		initTLogReq.allTags = tags;
		initTLogReq.isPrimary = true;
		initTLogReq.locality = pTLogTestContext->primaryLocality;
		initTLogReq.recoveryTransactionVersion = pTLogTestContext->initVersion;
	}

	TLogInterface interface = wait(promiseStream.getReply(initTLogReq));
	pTLogContext->TestTLogInterface = interface;
	pTLogContext->init = promiseStream;

	// inform other actors tLog is ready.
	pTLogContext->TLogCreated.send(true);

	TraceEvent("TestTLogServerInitializedTLog", pTLogContext->tLogID);

	// wait for either test completion or tLog failure.
	choose {
		when(wait(tl)) {}
		when(bool testCompleted = wait(pTLogContext->TestTLogServerCompleted.getFuture())) {
			ASSERT_EQ(testCompleted, true);
		}
	}

	wait(delay(1.0));

	// delete old disk queue files
	deleteFile(diskQueueFilename + "0." + tLogOptions.diskQueueExtension);
	deleteFile(diskQueueFilename + "1." + tLogOptions.diskQueueExtension);
	deleteFile(kvStoreFilename + "0." + tLogOptions.kvStoreExtension);
	deleteFile(kvStoreFilename + "1." + tLogOptions.kvStoreExtension);

	return Void();
}

ACTOR Future<Void> TLogTestContext::sendPushMessages(TLogTestContext* pTLogTestContext) {

	TraceEvent("TestTLogServerEnterPush", pTLogTestContext->workerID);

	state uint16_t logID = 0;
	for (logID = 0; logID < pTLogTestContext->numLogServers; logID++) {
		state Reference<TLogContext> pTLogContext = pTLogTestContext->pTLogContextList[logID];
		bool tLogReady = wait(pTLogContext->TLogStarted.getFuture());
		ASSERT_EQ(tLogReady, true);
	}

	state Version prev = pTLogTestContext->initVersion - 1;
	state Version next = pTLogTestContext->initVersion;
	state int i = 0;
	for (; i < pTLogTestContext->numCommits; i++) {
		Standalone<StringRef> key = StringRef(format("key %d", i));
		Standalone<StringRef> val = StringRef(format("value %d", i));
		MutationRef m(MutationRef::Type::SetValue, key, val);

		// build commit request
		LogPushData toCommit(pTLogTestContext->ls, pTLogTestContext->numLogServers /* tLogCount */);
		// UID spanID = deterministicRandom()->randomUniqueID();
		toCommit.addTransactionInfo(SpanContext());

		// for each tag
		for (uint32_t tagID = 0; tagID < pTLogTestContext->numTagsPerServer; tagID++) {
			Tag tag(pTLogTestContext->tagLocality, tagID);
			std::vector<Tag> tags = { tag };
			toCommit.addTags(tags);
			toCommit.writeTypedMessage(m);
		}
		Future<Version> loggingComplete = pTLogTestContext->ls->push(prev, next, prev, prev, toCommit, SpanContext());
		Version ver = wait(loggingComplete);
		ASSERT_LE(ver, next);
		prev++;
		next++;
	}

	TraceEvent("TestTLogServerExitPush", pTLogTestContext->workerID).detail("LogID", logID);

	return Void();
}

// send peek/pop through a given TLog interface and tag
ACTOR Future<Void> TLogTestContext::peekCommitMessages(TLogTestContext* pTLogTestContext,
                                                       uint16_t logID,
                                                       uint32_t tagID) {
	state Reference<TLogContext> pTLogContext = pTLogTestContext->pTLogContextList[logID];
	bool tLogReady = wait(pTLogContext->TLogStarted.getFuture());
	ASSERT_EQ(tLogReady, true);

	// peek from the same tag
	state Tag tag(pTLogTestContext->tagLocality, tagID);

	TraceEvent("TestTLogServerEnterPeek", pTLogTestContext->workerID).detail("LogID", logID).detail("Tag", tag);

	state Version begin = 1;
	state int i;
	for (i = 0; i < pTLogTestContext->numCommits; i++) {
		// wait for next message commit
		::TLogPeekRequest request(begin, tag, false, false);
		::TLogPeekReply reply = wait(pTLogContext->TestTLogInterface.peekMessages.getReply(request));
		TraceEvent("TestTLogServerTryValidateDataOnPeek", pTLogTestContext->workerID)
		    .detail("B", reply.begin.present() ? reply.begin.get() : -1);

		// validate versions
		ASSERT_GE(reply.maxKnownVersion, i);

		// deserialize package, first the version header
		ArenaReader rd = ArenaReader(reply.arena, reply.messages, AssumeVersion(g_network->protocolVersion()));
		ASSERT_EQ(*(int32_t*)rd.peekBytes(4), VERSION_HEADER);
		int32_t dummy; // skip past VERSION_HEADER
		Version ver;
		rd >> dummy >> ver;

		// deserialize transaction header
		int32_t messageLength;
		uint16_t tagCount;
		uint32_t sub = 1;
		if (FLOW_KNOBS->WRITE_TRACING_ENABLED) {
			rd >> messageLength >> sub >> tagCount;
			rd.readBytes(tagCount * sizeof(Tag));

			// deserialize span id
			if (sub == 1) {
				SpanContextMessage contextMessage;
				rd >> contextMessage;
			}
		}

		// deserialize mutation header
		if (sub == 1) {
			rd >> messageLength >> sub >> tagCount;
			rd.readBytes(tagCount * sizeof(Tag));
		}
		// deserialize mutation
		MutationRef m;
		rd >> m;

		// validate data
		Standalone<StringRef> expectedKey = StringRef(format("key %d", i));
		Standalone<StringRef> expectedVal = StringRef(format("value %d", i));
		ASSERT_WE_THINK(m.param1 == expectedKey);
		ASSERT_WE_THINK(m.param2 == expectedVal);

		TraceEvent("TestTLogServerValidatedDataOnPeek", pTLogTestContext->workerID)
		    .detail("Commit count", i)
		    .detail("LogID", logID)
		    .detail("TagID", tag);

		// go directly to pop as there is no SS.
		::TLogPopRequest requestPop(begin, begin, tag);
		wait(pTLogContext->TestTLogInterface.popMessages.getReply(requestPop));

		begin++;
	}

	TraceEvent("TestTLogServerExitPeek", pTLogTestContext->workerID).detail("LogID", logID).detail("TagID", tag);

	return Void();
}

ACTOR Future<Void> buildTLogSet(Reference<TLogTestContext> pTLogTestContext) {
	state TLogSet tLogSet;
	state uint16_t processID = 0;

	tLogSet.tLogLocalities.push_back(LocalityData());
	tLogSet.tLogPolicy = Reference<IReplicationPolicy>(new PolicyOne());
	tLogSet.locality = pTLogTestContext->primaryLocality;
	tLogSet.isLocal = true;
	tLogSet.tLogVersion = TLogVersion::V6;
	tLogSet.tLogReplicationFactor = 1;
	for (; processID < pTLogTestContext->numLogServers; processID++) {
		state Reference<TLogContext> pTLogContext = pTLogTestContext->pTLogContextList[processID];
		bool isCreated = wait(pTLogContext->TLogCreated.getFuture());
		ASSERT_EQ(isCreated, true);

		tLogSet.tLogs.push_back(OptionalInterface<TLogInterface>(pTLogContext->TestTLogInterface));
	}
	pTLogTestContext->dbInfo.logSystemConfig.tLogs.push_back(tLogSet);
	for (processID = 0; processID < pTLogTestContext->numLogServers; processID++) {
		Reference<TLogContext> pTLogContext = pTLogTestContext->pTLogContextList[processID];
		// start transactions
		pTLogContext->TLogStarted.send(true);
	}
	return Void();
}

// This test creates a tLog and pushes data to it. the If the recovery
// test switch is on, a new "generation" of tLogs is then created. These enter recover mode
// and pull data from the old generation. The data is peeked from either the old or new generation
// depending on the recovery switch, validated, and popped.

ACTOR Future<Void> startTestsTLogRecoveryActors(TestTLogOptions params) {
	state std::vector<Future<Void>> tLogActors;
	state Reference<TLogTestContext> pTLogTestContextEpochOne =
	    initTLogTestContext(params, Optional<Reference<TLogTestContext>>());

	FlowTransport::createInstance(false, 1, WLTOKEN_RESERVED_COUNT);

	state uint16_t tLogIdx = 0;

	TraceEvent("TestTLogServerEnterRecoveryTest");

	// Create the first "old" generation of tLogs
	Reference<TLogContext> pTLogContext(new TLogContext(tLogIdx));
	pTLogTestContextEpochOne->pTLogContextList.push_back(pTLogContext);
	tLogActors.emplace_back(
	    getTLogCreateActor(pTLogTestContextEpochOne, pTLogTestContextEpochOne->tLogOptions, tLogIdx));

	// wait for tLogs to be created, and signal pushes can start
	wait(buildTLogSet(pTLogTestContextEpochOne));

	PromiseStream<Future<Void>> promises;
	pTLogTestContextEpochOne->ls = ILogSystem::fromServerDBInfo(
	    pTLogTestContextEpochOne->logID, pTLogTestContextEpochOne->dbInfo, false, promises);

	wait(pTLogTestContextEpochOne->sendPushMessages());

	if (!pTLogTestContextEpochOne->recover) {
		wait(pTLogTestContextEpochOne->peekCommitMessages(0, 0));
	} else {
		// Done with old generation. Lock the old generation of tLogs.
		TLogLockResult data = wait(
		    pTLogTestContextEpochOne->pTLogContextList[tLogIdx]->TestTLogInterface.lock.getReply<TLogLockResult>());
		TraceEvent("TestTLogServerLockResult").detail("KCV", data.knownCommittedVersion);

		state Reference<TLogTestContext> pTLogTestContextEpochTwo =
		    initTLogTestContext(TestTLogOptions(params), pTLogTestContextEpochOne);
		Reference<TLogContext> pNewTLogContext(new TLogContext(tLogIdx));
		pTLogTestContextEpochTwo->pTLogContextList.push_back(pNewTLogContext);

		InitializeTLogRequest req;
		req.recruitmentID = pTLogTestContextEpochTwo->dbInfo.logSystemConfig.recruitmentID;
		req.recoverAt = pTLogTestContextEpochOne->numCommits;
		req.startVersion = pTLogTestContextEpochOne->initVersion + 1;
		req.recoveryTransactionVersion = pTLogTestContextEpochOne->initVersion;
		req.knownCommittedVersion = pTLogTestContextEpochOne->initVersion;
		req.epoch = pTLogTestContextEpochTwo->epoch;
		req.logVersion = TLogVersion::V6;
		req.locality = pTLogTestContextEpochTwo->primaryLocality;
		req.isPrimary = true;
		req.logRouterTags = 0;
		req.recoverTags = { Tag(pTLogTestContextEpochTwo->primaryLocality, 0) };
		req.recoverFrom = pTLogTestContextEpochOne->dbInfo.logSystemConfig;
		req.recoverFrom.logRouterTags = 0;

		const TestTLogOptions& tLogOptions = pTLogTestContextEpochTwo->tLogOptions;

		tLogActors.emplace_back(getTLogCreateActor(pTLogTestContextEpochTwo,
		                                           tLogOptions,
		                                           tLogIdx,
		                                           &req,
		                                           pTLogTestContextEpochOne->pTLogContextList[tLogIdx]->tLogID));

		state Reference<TLogContext> pTLogContext = pTLogTestContextEpochTwo->pTLogContextList[0];
		bool isCreated = wait(pTLogContext->TLogCreated.getFuture());
		ASSERT_EQ(isCreated, true);
		pTLogContext->TLogStarted.send(true);

		wait(pTLogTestContextEpochTwo->peekCommitMessages(0, 0));

		// signal that tLogs can be destroyed
		pTLogTestContextEpochTwo->pTLogContextList[tLogIdx]->TestTLogServerCompleted.send(true);
	}

	pTLogTestContextEpochOne->pTLogContextList[tLogIdx]->TestTLogServerCompleted.send(true);

	// wait for tLogs to destruct
	wait(waitForAll(tLogActors));

	TraceEvent("TestTLogServerExitRecoveryTest");

	return Void();
}

// TEST_CASE("/fdbserver/test/TestTLogCommits") {
// 	TestTLogOptions testTLogOptions(params);
// 	testTLogOptions.recover = 0;
// 	wait(startTestsTLogRecoveryActors(testTLogOptions));
// 	return Void();
// }

// TEST_CASE("/fdbserver/test/TestTLogRecovery") {
// 	TestTLogOptions testTLogOptions(params);
// 	wait(startTestsTLogRecoveryActors(testTLogOptions));
// 	return Void();
// }
