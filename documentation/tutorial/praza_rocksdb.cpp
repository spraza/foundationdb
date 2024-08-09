#include "rocksdb/options.h"
#include <iostream>
#include <cassert>
#include <fmt/format.h>
#include <rocksdb/db.h>

int main() {
	using namespace rocksdb;

	DB* db;
	Options options;
	options.create_if_missing = true;
	Status openStatus = DB::Open(options, "/tmp/testdb", &db);
	assert(openStatus.ok());

	std::string key = "key1";
	std::string val = "val1";
	{
		auto putStatus = db->Put(WriteOptions(), key, val);
		assert(putStatus.ok());
		std::cout << fmt::format("put key {} and val {}", key, val) << std::endl;
	}

	{
		std::string outVal;
		auto getStatus = db->Get(ReadOptions(), key, &outVal);
		assert(getStatus.ok());
		std::cout << fmt::format("for key {}, got value {}", key, outVal) << std::endl;
	}

	auto closeStatus = db->Close();
	assert(closeStatus.ok());
	delete db;

	return 0;
}