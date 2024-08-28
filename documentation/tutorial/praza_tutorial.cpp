#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <execinfo.h>
#include <cstdlib>
#include "flow/DeterministicRandom.h"
#include "flow/FileIdentifier.h"
#include "flow/Platform.h"
#include "flow/flow.h"

void printStackTrace() {
	const int maxFrames = 100;
	void* buffer[maxFrames];
	int numFrames = backtrace(buffer, maxFrames);

	// Get the strings that describe the addresses
	char** symbols = backtrace_symbols(buffer, numFrames);
	if (symbols == nullptr) {
		std::cerr << "Error: Unable to retrieve stack trace symbols." << std::endl;
		return;
	}

	// Print the stack trace
	std::cerr << "Stack Trace:" << std::endl;
	for (int i = 0; i < numFrames; ++i) {
		std::cerr << symbols[i] << std::endl;
	}

	// Free the memory allocated for symbols
	std::free(symbols);
}

void foo() {
	int x = 2;
	// std::cout << platform::get_backtrace() << std::endl;
	printStackTrace();
	std::cout << "hello " << x << std::endl;
}

int main() {
	foo();
	return 0;
}