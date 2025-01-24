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
#include <iostream>
#include <vector>
// #include <elfutils/libdwfl.h>
#include <functional>
#include <cxxabi.h>
#include <memory>
#include <string>

// ///
// std::string demangle(const char* name) {
// 	int status;
// 	char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
// 	if (status == 0) {
// 		std::string result(demangled);
// 		free(demangled);
// 		return result;
// 	}
// 	return name;
// }

// static const Dwfl_Callbacks proc_callbacks = { .find_elf = dwfl_linux_proc_find_elf,
// 	                                           .find_debuginfo = dwfl_standard_find_debuginfo,
// 	                                           .section_address = dwfl_offline_section_address,
// 	                                           .debuginfo_path = nullptr };

// std::string get_function_name(void* addr) {
// 	Dwfl* dwfl = dwfl_begin(&proc_callbacks);
// 	assert(dwfl);
// 	if (dwfl_linux_proc_report(dwfl, getpid()) != 0) {
// 		assert(false);
// 	}
// 	if (dwfl_report_end(dwfl, nullptr, nullptr) != 0) {
// 		assert(false);
// 	}
// 	Dwfl_Module* module = dwfl_addrmodule(dwfl, reinterpret_cast<Dwarf_Addr>(addr));
// 	assert(module);
// 	const char* name = dwfl_module_addrname(module, reinterpret_cast<Dwarf_Addr>(addr));
// 	assert(name);
// 	return std::string{ demangle(name) };
// }

// ///

// class StackTraceEntry {
// public:
// 	void* caller_address;
// 	std::string function_name;

// 	StackTraceEntry(void* addr, const std::string& name) : caller_address(addr), function_name(name) {}
// };

// using StackTrace = std::shared_ptr<std::vector<StackTraceEntry>>;

// class AsyncTask {
// public:
// 	std::function<void()> task;
// 	StackTrace stack_trace;

// 	template <typename Func>
// 	AsyncTask(Func&& f, StackTrace trace) : task(std::forward<Func>(f)), stack_trace(std::move(trace)) {}
// };

// class EventLoop {
// private:
// 	std::vector<AsyncTask> task_queue;

// public:
// 	template <typename Func>
// 	void push_task(Func&& func, StackTrace trace) {
// 		void* caller_address = __builtin_return_address(0);
// 		trace->emplace_back(caller_address, "unnamed_function");
// 		task_queue.emplace_back(std::forward<Func>(func), trace);
// 	}

// 	void run() {
// 		while (!task_queue.empty()) {
// 			auto task = std::move(task_queue.back());
// 			task_queue.pop_back();
// 			task.task();
// 		}
// 	}
// };

// EventLoop event_loop;

// void print_stack_trace(const StackTrace& trace) {
// 	std::cout << "Stack trace:\n";
// 	for (const auto& entry : *trace) {
// 		std::cout << "  " << get_function_name(entry.caller_address) << " at " << entry.caller_address << "\n";
// 	}
// 	std::cout << "\n";
// }

// void function3(StackTrace trace) {
// 	std::cout << "Function 3 executed\n";
// 	print_stack_trace(trace);
// }

// void function2(StackTrace trace) {
// 	event_loop.push_task([trace]() { function3(trace); }, trace);
// 	std::cout << "Function 2 executed\n";
// }

// void function1(StackTrace trace) {
// 	event_loop.push_task([trace]() { function2(trace); }, trace);
// 	std::cout << "Function 1 executed\n";
// }

int main() {
	// auto initial_trace = std::make_shared<std::vector<StackTraceEntry>>();
	// event_loop.push_task([initial_trace]() { function1(initial_trace); }, initial_trace);
	// event_loop.run();
	return 0;
}