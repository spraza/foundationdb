#include "flow/Buggify.h"
#include "fmt/format.h"
#include "flow/flow.h"
#include "flow/Platform.h"
#include "flow/Arena.h"
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

ACTOR Future<Void> hop3(int prev) {
	std::cout << prev + 1 << std::endl;
	wait(delay(1));
	return Void();
}

ACTOR Future<Void> hop2(int prev) {
	wait(hop3(prev + 1));
	return Void();
}

ACTOR Future<Void> hop1(int prev) {
	wait(hop2(prev + 1));
	return Void();
}

int main(int argc, char* argv[]) {
	platformInit();
	g_network = newNet2(TLSConfig(), false, true);
	auto x = stopAfter(hop1(0));
	g_network->run();
	return 0;
}
