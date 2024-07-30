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

struct Foo {
	static inline const Standalone<StringRef> DEFAULT_VERSIONSTAMP =
	    "\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00"_sr;
};

ACTOR Future<Void> foo() {
	wait(delay(1));
	return Void();
}

int main(int argc, char* argv[]) {
	platformInit();
	g_network = newNet2(TLSConfig(), false, true);
	auto x = stopAfter(foo());
	g_network->run();
	return 0;
}
