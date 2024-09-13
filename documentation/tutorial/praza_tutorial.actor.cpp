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

ACTOR Future<Void> bar(Reference<AsyncVar<int>> av) {
	loop {
		std::cout << "av.get() = " << av->get() << std::endl;
		wait(av->onChange()); // notificiation
		// wait(delay(1)); // poll every 1 second
	}
}

ACTOR Future<Void> foo() {
	state Reference<AsyncVar<int>> av = makeReference<AsyncVar<int>>(5);
	loop {
		choose {
			when(wait(bar(av))) {}
			// when(wait(delay(5))) {
			// 	av->set(av->get() + 5);
			// }
			when(wait(delay(2))) {
				// av->trigger();
			}
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
