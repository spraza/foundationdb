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
		rocksdb::Status status = rocksdb::DB::Open(options, path, &db);
		assert(status.ok());
		db->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), "MyRocksDBCheckpoint", &cf);
		db->Put(rocksdb::WriteOptions(), cf, "foo", "bar");
		db->DestroyColumnFamilyHandle(cf);
		db->Close();
	}
	std::vector<rocksdb::ColumnFamilyDescriptor> cfDescriptors;
	{
		std::vector<std::string> columnFamilies;
		rocksdb::Status status = rocksdb::DB::ListColumnFamilies(options, path, &columnFamilies);
		for (const std::string& name : columnFamilies) {
			cfDescriptors.emplace_back(name, rocksdb::ColumnFamilyOptions());
		}
	}
	{
		std::vector<rocksdb::ColumnFamilyHandle*> handles;
		handles.push_back(cf);
		rocksdb::Status status = rocksdb::DB::OpenForReadOnly(options, path, cfDescriptors, &handles, &db);
		std::string val;
		rocksdb::Status getStatus = db->Get(rocksdb::ReadOptions(), "foo", &val);
		std::cout << fmt::format("get({}) = {}", "foo", val);
		db->DestroyColumnFamilyHandle(cf);
		db->Close();
	}
	// std::cout << fmt::format("my str is {}", "blah") << std::endl;
	return 0;
}