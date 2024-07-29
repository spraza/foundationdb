#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

void print_env_variables() {
	char** env = environ;
	while (*env) {
		std::cout << *env << std::endl;
		env++;
	}
}

int main() {
	std::cout << "*** PARENT START, pid = " << getpid() << std::endl;
	setenv("PARENT_SPECIFIC_BEFORE_CHILD_CREATION", "parent_value", 1);
	std::cout << "*** PARENT ENV VARIABLES" << std::endl;
	print_env_variables();

	pid_t pid = fork();
	assert(pid != -1);
	if (pid == 0) { // child
		std::cout << "*** CHILD START, pid = " << getpid() << std::endl;
		setenv("CHILD_SPECIFIC", "child_value", 1);
		std::cout << "*** CHILD ENV VARIABLES" << std::endl;
		print_env_variables();
		std::cout << "*** CHILD END" << std::endl;
	} else { // parent
		setenv("PARENT_SPECIFIC_AFTER_CHILD_CREATION", "parent_value", 1);
		waitpid(pid, nullptr, 0);
		std::cout << "*** PARENT END" << std::endl;
	}

	return 0;
}