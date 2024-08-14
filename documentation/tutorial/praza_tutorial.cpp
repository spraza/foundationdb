#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include "flow/DeterministicRandom.h"
#include "flow/Platform.h"

int main() {
	auto seed = 132; // platform::getRandomSeed()
	auto d = DeterministicRandom(seed);
	for (int i = 0; i < 10; ++i) {
		std::cout << d.randomUInt64() << std::endl;
	}
	return 0;
}