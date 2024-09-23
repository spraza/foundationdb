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
#include <stdexcept>
#include <string>
#include <thread>
// #include <chrono>
#include <unordered_map>
#include <memory>
#include <vector>
#include <iostream>

#include "flow/actorcompiler.h"

// Learnings:
//    1. throw is understood by actor compilers, and it expects flow Error type, not type std errors/exceptions
//    2. depending on the error thrown, the actor compiler decides to crash the program or not (in which case, <no_op?>,
//    but can be caught explicitly if you want)
//    3. from actor readme.md:
//          By default it is a compiler error to discard the result of a cancellable actor. If you don't think this is
//          appropriate for your actor you can use the `[[flow_allow_discard]]` attribute. This does not apply to
//          UNCANCELLABLE actors.
//          related: also say "[[nodiscard]]" in the code. someday if needed, i can spend the time to understand this
//          better
//    4. how to cancel as a "client" of actor:
//        (a) actor compiler cancels automatically based on scope of actor or any of
//            its parents, where parents are defined as any actor instance that calls into the self actor instance - the
//            parent can decide to wait on the current/self actor, in which case the current/self actor will be
//            completed/ready first.
//        (b) the future you get back from actor, just do f.cancel(). This is probably what actor compiler does anyway.
//    5. on actor side, what happens in the event of cancelation:
//         if you have a try-catch block in the actor, and the parent cancels it (because of timeout/scope or not
//         waiting) if you don't have a try-catch, then nothing happens and unless blocked, the actor will exit if the
//         actor is blocked on some synchoronous code, that will have to finish first before cancellation exits another
//         way to think about it: cancelation would happen before the next "wait"
//    6. if you don't want on an actor, its errors (if any) are not propagated to you as the parent
//    7. is cancellation cooperative? e.g. if client cancels, does actor have a choice?
//            ans: actor does not have a choice, it always gets the error exception, so unless there is a weird way
//            where actor catches exception and then somehow block or recursively call itself, mostly the actor will
//            have to exit out. In fact, if you don't explicitly have code for error handling in actor, then basically
//            you'll exit out anyway.
//    8. how to make actor uncancellable? ACTOR UNCANCELLBALE keyword, or return void instead of Future<Void>
//         what happens in these cases is weird: actor does not get the error but still exits.. someday if needed i can
//         spend the time to understand this better

ACTOR UNCANCELLABLE Future<Void> bar(int x) {
	//	using namespace std::chrono_literals;
	try {
		// std::this_thread::sleep_for(10s); // will delay cancelation
		wait(delay(10));
		if (x > 0) {
			// throw not_implemented();
			throw internal_error();
		}
	} catch (Error& e) {
		std::cout << "bar1\n";
		std::cout << e.name() << ", " << e.code() << ", " << e.what() << std::endl;
	}
	return Void();
}

ACTOR Future<Void> foo() {
	try {
		wait(delay(3));
		// wait(bar(1));
		state Future<Void> f = bar(1);
		std::cout << "waiting...\n";
		wait(delay(2));
		f.cancel();
		// wait(f);
	} catch (Error& e) {
		std::cout << "foo1\n";
		std::cout << e.name() << ", " << e.code() << ", " << e.what() << std::endl;
		return Void();
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
