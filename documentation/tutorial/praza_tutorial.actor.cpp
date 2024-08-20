#include "fdbrpc/FlowTransport.h"
#include "fdbrpc/fdbrpc.h"
#include "flow/Buggify.h"
#include "flow/TaskPriority.h"
#include "flow/genericactors.actor.h"
#include "fmt/format.h"
#include "flow/flow.h"
#include "flow/Platform.h"
#include "flow/Arena.h"
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

// Based on https://drive.google.com/file/d/1C4piNHl7NRzHHzFomMKW3_uSK9Yw3_Kr/view?usp=sharing

// SAV<T>, single assignment variable, variable type is T
// send() - sets the value, asserts if isSet() is true
// get() or value() - gets the value
// isSet() - useful to see before sending
// SAV also has a chain of callbacks which typically
// a Future (promise.getFuture) can register
// In practice, the ACTOR compiler generates code that does
// this callback registration, basically post-wait piece of code
// is registered as a callback
// wait() adds to the "front" of the callback list in SAV
// meaning when we fire the SAV callbacks, it is LIFO
// yields adds at the end, making if FIFO

// Future, read-only reference to SAV
// We can wait on future, and:
// Must (May?) be used to register callbacks if the SAV is not set yet
// Blocks on callback registration, actual callback runs later when
// SAV becomes ready (via promise.send())
// If SAV ready already, wait on future unblocks quickly and so no
// callback registration is done by ACTOR compiler

// Promise, write-only reference to SAV
// These primitives are used to communication between ACTORS
// but can be used in general outside ACTOR functions
// These are basically at the end just flow (target) classes
// in flow.h
// function is actor if it says ACTOR, additionally mostly it should
// also use wait() on another actor

ACTOR Future<Void> ex1_1(Promise<int> p) {
	// Default copy-ctor, shallow copy i.e. same SAV shared across promises
	// SAV's internal promise count = 2, future count = 0
	Promise<int> p1 = p;

	// From any promise, you can get corresponding future
	// This references the same SAV, we bump future count = 1
	Future<int> f1 = p1.getFuture();
	int x = wait(f1);

	std::cout << "x = " << x << ", ex1_1 code" << std::endl;

	return Void();
}

ACTOR Future<Void> ex1_2(Promise<int> p) {
	Future<int> f1 = p.getFuture();
	int y = wait(f1);
	std::cout << "y = " << y << ", ex1_2 code" << std::endl;
	return Void();
}

ACTOR Future<Void> ex1() {
	// Creates an internal SAV<int>
	// The SAV has an internal promise and future count
	// Here, promise count = 1, future count = 0
	// From SAV doc:
	// int promises; // one for each promise (and one for an active actor if this is an actor)
	// int futures; // one for each future and one more if there are any callbacks
	// Q: why is future count not incremented more than when adding more than 1 callbacks?
	// A: cz these counts are used as reference counts to determine when to safely destroy the SAV.
	//    so when all callbacks are fired, future is decremented by 1.
	// There's also error_state, it's un-named enum (so just int)
	// enum { UNSET_ERROR_CODE = -3, NEVER_ERROR_CODE, SET_ERROR_CODE };
	// And an error object created e.g. auto error = Error::fromCode(UNSET_ERROR_CODE)
	// error_state = unset
	state Promise<int> p1;

	Future<Void> f1 = ex1_1(p1);
	Future<Void> f2 = ex1_2(p1);
	state std::vector<Future<Void>> all({ f1, f2 });

	wait(delay(2));
	p1.send(10);

	wait(waitForAll(all));
	return Void();
}

ACTOR Future<Void> ex2_producer(PromiseStream<int> p, int startCount) {
	state int i = startCount;
	loop {
		wait(delay(2));
		p.send(i);
		i += 1;
	}
}

ACTOR Future<Void> ex2_consumer(FutureStream<int> f, int id) {
	loop {
		int x = waitNext(f);
		std::cout << "Consumer id " << id << " consumed " << x << std::endl;
	}
}

// multiple producer multiple consumer
ACTOR Future<Void> ex2() {
	// Similar to Promise, except it references a NotifiedQueue (instead of SAV)
	// Each element of NotifiedQueue is consumed only once, using waitNext
	// This is a big difference, previously when value 5 is sent to Promise, SAV
	// becomes "set"/ready, and then fires all callbacks (in LIFO / stack) and forwards
	// the value 5 to those callbacks. These callbacks execute post-wait chunk of code.
	// But here, only one waitNext() will get the value produced by promiseStream.send().
	// However, unlike SAV, multiple elements can be sent to NotifiedQueue, so there's that.
	PromiseStream<int> p;

	// Can get future stream from promise stream
	FutureStream<int> f = p.getFuture();
	auto fp1 = ex2_producer(p, 0);
	// auto fp2 = ex2_producer(p, 100);
	auto fc1 = ex2_consumer(f, 0);
	// auto fc2 = ex2_consumer(f, 1);
	// auto fc3 = ex2_consumer(f, 2);

	// std::vector<Future<Void>> all({ fp1, fp2, fc1, fc2, fc3 });
	std::vector<Future<Void>> all({ fp1, fc1 });
	wait(waitForAll(all));

	return Void();
}

ACTOR Future<Void> start() {
	// std::vector<Future<Void>> all{ ex1(), ex2() };
	std::vector<Future<Void>> all{ ex2() };
	wait(waitForAll(all));
	return Void();
}

int main(int argc, char* argv[]) {
	platformInit();
	g_network = newNet2(TLSConfig(), false, true);
	auto x = stopAfter(start());
	g_network->run();
	std::cout << "Done\n";
	return 0;
}
