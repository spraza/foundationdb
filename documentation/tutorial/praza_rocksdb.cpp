#include "rocksdb/options.h"
#include <iostream>
#include <cassert>
#include <fmt/format.h>
#include <rocksdb/db.h>

int main() {
	rocksdb::DB* db;
	const std::string path{ "/tmp/testdb" };
	rocksdb::Options options;
	options.create_if_missing = true;
	rocksdb::ColumnFamilyHandle* cf;
	{
		auto status = rocksdb::DB::Open(options, path, &db);
		assert(status.ok());
		auto status2 = db->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), "MyRocksDBCheckpoint", &cf);
		assert(status2.ok());
		assert(db->Put(rocksdb::WriteOptions(), cf, "foo", "bar").ok());
		assert(db->DestroyColumnFamilyHandle(cf).ok());
		assert(db->Close().ok());
	}
	std::vector<rocksdb::ColumnFamilyDescriptor> cfDescriptors;
	{
		std::vector<std::string> columnFamilies;
		assert(rocksdb::DB::ListColumnFamilies(options, path, &columnFamilies).ok());
		for (const std::string& name : columnFamilies) {
			cfDescriptors.emplace_back(name, rocksdb::ColumnFamilyOptions());
		}
	}
	{
		std::vector<rocksdb::ColumnFamilyHandle*> handles;
		handles.push_back(cf);
		auto status = rocksdb::DB::OpenForReadOnly(options, path, cfDescriptors, &handles, &db);
		assert(status.ok());
		std::string val;
		assert(db->Get(rocksdb::ReadOptions(), cf, "foo", &val).ok());
		std::cout << fmt::format("get({}) = {}", "foo", val);
		assert(db->Close().ok());
	}
	// std::cout << fmt::format("my str is {}", "blah") << std::endl;
	return 0;
}