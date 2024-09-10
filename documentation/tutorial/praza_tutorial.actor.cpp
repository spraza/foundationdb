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

struct State {
	State() {
		std::cout << "State ctor default"
		          << "\n";
	}
	State(std::string type) : type(type) { std::cout << "State ctor explicit, type: " << type << "\n"; }
	std::string type;
	~State() { std::cout << "State dtor, type: " << type << "\n"; }
	void p() { std::cout << "State print, type: " << type << "\n"; }
};

ACTOR Future<Void> baz() {
	std::cout << "baz enter\n";
	state State s("baz");
	wait(delay(1));
	s.p();
	std::cout << "baz exit\n";
	return Void();
}

ACTOR Future<Void> bar() {
	std::cout << "bar enter\n";
	state State s("bar");
	wait(delay(2));
	s.p();
	std::cout << "bar exit\n";
	return Void();
}

ACTOR Future<Void> foo() {
	state Future<Void> x = bar();
	state Future<Void> y = baz();
	loop choose {
		when(wait(x)) {
			std::cout << "foo1\n";
			// y.cancel();
			break;
		}
		when(wait(y)) {
			std::cout << "foo2\n";
			// x.cancel();
			break;
		}
	}
	return Void();
}

int main(int argc, char* argv[]) {
	platformInit();
	g_network = newNet2(TLSConfig(), false, true);
	auto x = stopAfter(foo());
	g_network->run();
	return 0;
}
