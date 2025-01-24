/*
* tutorial.actor.cpp

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

#include "fmt/format.h"
#include "flow/flow.h"
#include "flow/Platform.h"
#include "flow/DeterministicRandom.h"
#include "fdbclient/NativeAPI.actor.h"
#include "fdbclient/ReadYourWrites.h"
#include "flow/TLSConfig.actor.h"
#include <functional>
#include <unordered_map>
#include <memory>
#include <iostream>
#include "flow/actorcompiler.h"

NetworkAddress serverAddress;

enum TutorialWellKnownEndpoints {
	WLTOKEN_SIMPLE_KV_SERVER = WLTOKEN_FIRST_AVAILABLE,
	WLTOKEN_ECHO_SERVER,
	WLTOKEN_COUNT_IN_TUTORIAL
};

ACTOR Future<Void> simpleTimer() {
	// we need to remember the time when we first
	// started.
	// This needs to be a state-variable because
	// we will use it in different parts of the
	// actor. If you don't understand how state
	// variables work, it is a good idea to remove
	// the state keyword here and look at the
	// generated C++ code from the actor compiler.
	state double start_time = g_network->now();
	loop {
		wait(delay(1.0));
		std::cout << format("Time: %.2f\n", g_network->now() - start_time);
	}
}

int main(int argc, char* argv[]) {
	bool isServer = false;
	platformInit();
	g_network = newNet2(TLSConfig(), false, true);
	FlowTransport::createInstance(!isServer, 0, WLTOKEN_COUNT_IN_TUTORIAL);
	auto f = stopAfter(simpleTimer());
	g_network->run();
	return 0;
}
