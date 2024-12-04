/*
 * BulkLoadingS3Client.actor.cpp
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

#include "fdbclient/NativeAPI.actor.h"
#include "fdbclient/S3Client.actor.h"
#include "fdbserver/workloads/workloads.actor.h"
#include "flow/actorcompiler.h" // This must be the last #include.


struct BulkLoadingS3Client : TestWorkload {
	static constexpr auto NAME = "BulkLoadingS3ClientWorkload";
	int masterPort;
	int s3Port;
	const std::string s3Bucket = "BulkLoadingS3Client_bucket";
	bool verbose;
	const bool enabled;
	bool pass;

	BulkLoadingS3Client(WorkloadContext const& wcx) : TestWorkload(wcx), enabled(true), pass(true) {
		masterPort = getOption(options, "masterPort"_sr, 9333);
		s3Port = getOption(options, "s3Port"_sr, 9333);
		verbose = getOption(options, "verbose"_sr, false);
	}

	// This method will be called by the tester during the setup phase. It should be used to populate the database.
	Future<Void> setup(Database const& cx) override {
		if (clientId != 0) {
			return Void();
		}
		return _setup(cx, this);
	}

	ACTOR Future<Void> _setup(Database cx, BulkLoadingS3Client* self) {
		return Void();
	}

	// When the tester completes, this method will be called. A workload should run any consistency/correctness
	// tests during this phase.
	Future<bool> check(Database const& cx) override {
		if (clientId != 0) {
			return true;
		}	
		return _check(cx, this);
	}

	ACTOR static Future<bool> _check(Database cx, BulkLoadingS3Client* self) {
		return true;
	}

	// If a workload collects metrics (like latencies or throughput numbers), these should be reported back here.
   	// The multitester (or test orchestrator) will collect all metrics from all test clients and it will aggregate them.
	void getMetrics(std::vector<PerfMetric>& m) override {}

	// This method should run the actual test.
	Future<Void> start(Database const& cx) override {
		if (clientId != 0) {
			return Void();
		}
		TraceEvent(SevInfo, "BLS3C_Param").detail("MasterPort", masterPort).detail("S3Port", s3Port).detail("Verbose", verbose);
		return _start(this, cx);
	}

	ACTOR static Future<bool> checkForSeaweed(std::string s3url) {
		std::string resource;
		state S3BlobStoreEndpoint::ParametersT parameters;
			std::string error;
		Reference<S3BlobStoreEndpoint> endpoint =
			S3BlobStoreEndpoint::fromString(s3url, {}, &resource, &error, &parameters);
		if (error.size()) {
			TraceEvent(SevError, "CheckForSeaweedGetEndpointError").detail("s3url", s3url).detail("error", error);
			throw backup_invalid_url();
		}
		// Test seaweedfs is up.
		// curl -L -vvv  http://localhost:9334/dir/assign
		HTTP::Headers headers;
		Reference<HTTP::IncomingResponse> response = wait(endpoint->doRequest("GET", "/dir/assign", headers, nullptr, 0, { 200 }));
		bool exists = response ->code == 200;
		TraceEvent("CheckForSeaweed").detail("url", s3url).detail("exists", exists);
		return exists;
	}

	ACTOR Future<Void> _start(BulkLoadingS3Client* self, Database cx) {
		if (self->clientId != 0) {
			// Our simulation test can trigger multiple same workloads at the same time
			// Only run one time workload in the simulation
			return Void();
		}

		/* Need to have an alive seaweed running with simulation */
		
		state std::string url = BLOBSTORE_PREFIX + "localhost:" + std::to_string(self->masterPort) +
			"/BulkLoadingS3Client?bucket=" + self->s3Bucket + "&region=us&secure_connection=0";
		bool seaweedIsUp = wait(checkForSeaweed(url));
		if (!seaweedIsUp) {
			printf("Seaweed is not up -- no BulkLoadingS3Client tests run.\n");
			TraceEvent(SevError, "BLS3C_NoSeaweed").detail("url", url);
			return Void();
		}
		/* Add S3 function call here */
		TraceEvent("BulkLoadingS3ClientWorkloadStart");
		wait(delay(0.1)); // code place holder for passing ACTOR compiling

		// Run this workload with ../build_output/bin/fdbserver -r simulation -f
		// ../src/foundationdb/tests/fast/BulkLoadingS3Client.toml

		return Void();
	}
};

WorkloadFactory<BulkLoadingS3Client> BulkLoadingS3ClientFactory;
