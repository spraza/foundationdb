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
	options.error_if_exists = false;

	// Step 1: Open the database and create a column family
	rocksdb::ColumnFamilyHandle* cf = nullptr;
	{
		rocksdb::Status status = rocksdb::DB::Open(options, path, &db);
		assert(status.ok());
		status = db->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), "RocksDBCheckpoint", &cf);
		assert(status.ok());
		status = db->Put(rocksdb::WriteOptions(), cf, "foo", "bar");
		assert(status.ok());

		// Do not drop the column family here
		db->DestroyColumnFamilyHandle(cf);
		db->Close();
		delete db;
	}

	// Step 2: List column families
	std::vector<rocksdb::ColumnFamilyDescriptor> cfDescriptors;
	{
		std::vector<std::string> columnFamilies;
		rocksdb::Status status = rocksdb::DB::ListColumnFamilies(options, path, &columnFamilies);
		assert(status.ok());
		for (const std::string& name : columnFamilies) {
			cfDescriptors.emplace_back(name, rocksdb::ColumnFamilyOptions());
		}
	}

	// Step 3: Open the database for read-only access and read the key
	{
		std::vector<rocksdb::ColumnFamilyHandle*> handles;
		rocksdb::Status status = rocksdb::DB::OpenForReadOnly(options, path, cfDescriptors, &handles, &db);
		assert(status.ok());

		// Find the handle for "RocksDBCheckpoint"
		rocksdb::ColumnFamilyHandle* targetHandle = nullptr;
		for (size_t i = 0; i < cfDescriptors.size(); ++i) {
			if (cfDescriptors[i].name == "RocksDBCheckpoint") {
				targetHandle = handles[i];
				break;
			}
		}

		if (targetHandle) {
			std::string val;
			rocksdb::Status getStatus = db->Get(rocksdb::ReadOptions(), targetHandle, "foo", &val);
			if (getStatus.ok()) {
				std::cout << fmt::format("get({}) = {}", "foo", val) << std::endl;
			} else {
				std::cout << "Key not found\n";
			}
		} else {
			std::cout << "Column family 'RocksDBCheckpoint' not found\n";
		}

		// Clean up handles
		for (auto handle : handles) {
			db->DestroyColumnFamilyHandle(handle);
		}
		db->Close();
		delete db;
	}

	return 0;
}
