#include "fdbrpc/FlowTransport.h"
#include "fdbrpc/fdbrpc.h"
#include "flow/Buggify.h"
#include "flow/Error.h"
#include "flow/FastRef.h"
#include "flow/NetworkAddress.h"
#include "flow/TaskPriority.h"
#include "flow/genericactors.actor.h"
#include "fmt/format.h"
#include "flow/flow.h"
#include "flow/Platform.h"
#include "flow/Arena.h"
#include "fdbclient/NativeAPI.actor.h"
#include "fdbclient/ReadYourWrites.h"
#include "flow/TLSConfig.actor.h"
#include <cstdint>
#include <functional>
#include <new>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <iostream>

#include "flow/actorcompiler.h"

int x{ 0 };

ACTOR Future<Void> measure() {
	wait(delay(0.1));
	std::cout << x << std::endl;
	return Void();
}

ACTOR Future<Void> increase() {
	wait(delay(5));
	x += 1;
	wait(Never());
	UNREACHABLE();
	return Void();
}

ACTOR Future<Void> foo() {
	state Future<Void> f = increase();
	loop choose {
		when(wait(delay(3))) {
			wait(measure());
		}
		when(wait(f)) {
			std::cout << "bar\n";
		}
	}
}

int main(int argc, char* argv[]) {
	platformInit();
	g_network = newNet2(TLSConfig(), false, true);
	auto x = stopAfter(foo());
	g_network->run();
	return 0;
}
