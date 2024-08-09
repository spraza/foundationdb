#include <iostream>
#include <cassert>
#include <fmt/format.h>
#include <rocksdb/db.h>

int main() {
	rocksdb::DB* db;
	rocksdb::Options options;
	options.create_if_missing = true;
	rocksdb::Status status = rocksdb::DB::Open(options, "/tmp/testdb", &db);
	assert(status.ok());
	std::cout << fmt::format("my str is {}", "blah") << std::endl;
	return 0;
}