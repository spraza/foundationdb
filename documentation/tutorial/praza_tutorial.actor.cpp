#include "flow/Buggify.h"
#include "fmt/format.h"
#include "flow/flow.h"
#include "flow/Platform.h"
#include "flow/DeterministicRandom.h"
#include "fdbclient/NativeAPI.actor.h"
#include "fdbclient/ReadYourWrites.h"
#include "flow/TLSConfig.actor.h"
#include <functional>
#include <new>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <iostream>
#include "flow/actorcompiler.h"

namespace {
bool DEBUG = true;

void print(const std::string& s) {
	if (DEBUG) {
		fmt::print("{}\n", s);
	}
}

} // namespace

ACTOR Future<double> io_num() {
	print("io_num: start...");
	wait(delay(1));
	print("io_num: done...");
	return 1;
}

ACTOR Future<Void> flow_timer() {
	print("flow_timer: start...");
	state double x = wait(io_num());
	print("x: " + std::to_string(x));
	state double start = g_network->now();
	loop {
		wait(delay(x));
		print("elapsed time: " + std::to_string(g_network->now() - start));
	}
}

ACTOR Future<Void> flow_future(Future<int> ready) {
	int x = wait(ready);
	print("x: " + std::to_string(x));
	return Void();
}

ACTOR Future<Void> flow_promise() {
	Promise<int> promise;
	Future<Void> fut = flow_future(promise.getFuture());
	wait(fut);
	return Void();
}

ACTOR Future<Void> nested_delay() {
	print("nested_delay: enter");
	wait(delay(3));
	print("nested_delay: exit");
	return Void();
}

ACTOR Future<Void> my_delay() {
	print("my_delay: enter");
	wait(delay(3));
	print("my_delay: mid");
	wait(nested_delay());
	print("my_delay: exit");
	return Void();
}

ACTOR Future<Void> test() {
	print("test: enter");
	state Future<Void> f = my_delay();
	print("f is ready: " + std::to_string(f.isReady()));
	wait(f);
	print("f is ready: " + std::to_string(f.isReady()));
	print("test: exit");
	return Void();
}

std::unordered_map<std::string, std::function<Future<Void>()>> actors = { { "timer", &flow_timer },
	                                                                      { "promise", &flow_promise },
	                                                                      { "test", &test } };

int main(int argc, char* argv[]) {
	// Start up
	platformInit();
	g_network = newNet2(TLSConfig(), false, true);

	// Decide which actor to run
	std::function<Future<Void>()> toRun;
	toRun = actors.at(argv[1]);

	// Run and wait
	auto f = stopAfter(toRun());

	// Before exiting
	g_network->run();
	return 0;
}
