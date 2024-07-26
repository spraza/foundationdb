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

ACTOR Future<Void> baz() {
	fmt::print("baz_start\n");
	wait(delay(5));
	fmt::print("baz_end\n");
	return Void();
}

ACTOR Future<Void> bar() {
	fmt::print("bar_start\n");
	wait(baz());
	fmt::print("bar_end\n");
	return Void();
}

ACTOR Future<Void> foo() {
	fmt::print("foo_start\n");
	wait(bar());
	fmt::print("foo_end\n");
	return Void();
}

int main(int argc, char* argv[]) {
	platformInit();
	g_network = newNet2(TLSConfig(), false, true);
	auto x = stopAfter(foo());
	g_network->run();
	return 0;
}
