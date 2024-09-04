#include "fdbrpc/FlowTransport.h"
#include "fdbrpc/fdbrpc.h"
#include "flow/Buggify.h"
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

ACTOR Future<Void> baz(int d) {
	state int x = d;
	wait(delay(x));
	x = x + 1;
	wait(delay(x));
	return Void();
}

ACTOR Future<Void> bar(int d) {
	state int x = d;
	wait(baz(x));
	x = x + 1;
	wait(baz(x));
	return Void();
}

ACTOR Future<Void> foo(int d) {
	state int x = d;
	wait(bar(x));
	x = x + 1;
	wait(bar(x));
	return Void();
}

int main(int argc, char* argv[]) {
	platformInit();
	g_network = newNet2(TLSConfig(), false, true);
	auto x = stopAfter(foo(1));
	g_network->run();
	return 0;
}
