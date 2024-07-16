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

// this is a simple actor that will report how long
// it is already running once a second.
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

std::unordered_map<std::string, std::function<Future<Void>()>> actors = {
	{ "timer", &simpleTimer }, // ./tutorial timer
}; // ./tutorial -C $CLUSTER_FILE_PATH fdbStatusStresser

int main(int argc, char* argv[]) {
	std::vector<std::function<Future<Void>()>> toRun;
	// parse arguments
	for (int i = 1; i < argc; ++i) {
		std::string arg(argv[i]);
		auto actor = actors.find(arg);
		if (actor == actors.end()) {
			std::cout << format("Error: actor %s does not exist\n", arg.c_str());
			return 1;
		}
		toRun.push_back(actor->second);
	}
	platformInit();
	g_network = newNet2(TLSConfig(), false, true);
	std::vector<Future<Void>> all;
	all.reserve(toRun.size());
	for (auto& f : toRun) {
		all.emplace_back(f());
	}
	auto f = stopAfter(waitForAll(all));
	g_network->run();
	return 0;
}
