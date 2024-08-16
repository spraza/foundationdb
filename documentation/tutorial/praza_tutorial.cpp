#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include "flow/DeterministicRandom.h"
#include "flow/FileIdentifier.h"
#include "flow/Platform.h"
#include "flow/flow.h"

int main() {
	Promise<int> p;
	Future<int> f = p.getFuture();
	p.send(6);
	std::cout << f.get() << std::endl;
	return 0;
}