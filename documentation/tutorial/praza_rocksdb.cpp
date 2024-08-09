#include "rocksdb/options.h"
#include "rocksdb/statistics.h"
#include <iostream>
#include <cassert>
#include <fmt/format.h>
#include <rocksdb/db.h>
#include <rocksdb/iostats_context.h>
#include <rocksdb/perf_context.h>

void bulk_write(rocksdb::DB* db, int numKeys) {
	using namespace rocksdb;
	for (int i = 0; i < numKeys; ++i) {
		auto putStatus = db->Put(WriteOptions(), fmt::format("key{}", i + 1), fmt::format("val{}", i + 1));
		assert(putStatus.ok());
	}
}

// void testing() {
// 	using namespace rocksdb;

// 	std::string key = "key1";
// 	std::string val = "val1";
// 	{
// 		auto putStatus = db->Put(WriteOptions(), key, val);
// 		assert(putStatus.ok());
// 		std::cout << fmt::format("put key {} and val {}", key, val) << std::endl;
// 	}

// 	{
// 		std::string outVal;
// 		auto getStatus = db->Get(ReadOptions(), key, &outVal);
// 		assert(getStatus.ok());
// 		std::cout << fmt::format("for key {}, got value {}", key, outVal) << std::endl;
// 	}
// }

int main() {
	using namespace rocksdb;

	// Open db
	DB* db;
	Options options;
	options.create_if_missing = true;
	options.statistics = rocksdb::CreateDBStatistics();
	options.statistics->set_stats_level(StatsLevel::kAll);
	Status openStatus = DB::Open(options, "/tmp/testdb", &db);
	assert(openStatus.ok());

	// Bulk write with profiling
	rocksdb::SetPerfLevel(PerfLevel::kEnableTime);
	get_perf_context()->Reset();
	get_iostats_context()->Reset();
	bulk_write(db, 10);
	rocksdb::SetPerfLevel(rocksdb::PerfLevel::kDisable);
	std::cout << get_perf_context()->ToString() << "\n\n-------------\n\n"
	          << get_iostats_context()->ToString() << std::endl;

	// Dump overall stats
	std::cout << "\n\nDB stats below:\n";
	std::cout << "BLOCK_CACHE_BYTES_WRITE: " << options.statistics->getTickerCount(Tickers::BLOCK_CACHE_BYTES_WRITE)
	          << std::endl;
	std::cout << "SST_WRITE_MICROS: " << options.statistics->getHistogramString(Histograms::SST_WRITE_MICROS)
	          << std::endl;

	// Close db
	auto closeStatus = db->Close();
	assert(closeStatus.ok());
	delete db;

	return 0;
}