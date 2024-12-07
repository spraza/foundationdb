#define FDB_USE_LATEST_API_VERSION
#include <foundationdb/fdb_c.h>
#include <iostream>
#include <string>
#include <cstdlib>

void checkError(fdb_error_t err, const char* message) {
	if (err) {
		std::cerr << "Error (" << err << "): " << message << std::endl;
		exit(EXIT_FAILURE);
	}
}

int main() {
	fdb_error_t err;

	// Setup
	checkError(fdb_select_api_version(FDB_API_VERSION), "Failed to select API version");
	checkError(fdb_setup_network(), "error1");
	checkError(
	    fdb_network_set_option(FDBNetworkOption::FDB_NET_OPTION_TRACE_ENABLE, reinterpret_cast<const uint8_t*>(""), 0),
	    "error2");
	checkError(fdb_network_set_option(
	               FDBNetworkOption::FDB_NET_OPTION_TRACE_FORMAT, reinterpret_cast<const uint8_t*>("json"), 4),
	           "error3");

	// Create db
	FDBDatabase* db;
	checkError(fdb_create_database("/tmp/local-cluster/loopback-cluster/fdb.cluster", &db),
	           "Failed to create database");

	// Key-Value Operations
	const std::string key = "my_key";
	const std::string value = "Hello, FoundationDB!";

	// Start a transaction
	FDBTransaction* transaction;
	checkError(fdb_database_create_transaction(db, &transaction), "error4");

	// Set the key-value pair
	fdb_transaction_set(
	    transaction, (const uint8_t*)key.c_str(), key.size(), (const uint8_t*)value.c_str(), value.size());

	// Commit the transaction
	FDBFuture* commitFuture = fdb_transaction_commit(transaction);
	err = fdb_future_block_until_ready(commitFuture);
	checkError(err, "Failed to commit transaction");
	fdb_future_destroy(commitFuture);

	std::cout << "Set key: " << key << ", value: " << value << std::endl;

	// Read back the key
	FDBFuture* getFuture = fdb_transaction_get(transaction, (const uint8_t*)key.c_str(), key.size(), 0);
	err = fdb_future_block_until_ready(getFuture);
	checkError(err, "Failed to get value");

	const uint8_t* outValue;
	int outValueLen;
	err = fdb_future_get_value(getFuture, nullptr, &outValue, &outValueLen);
	checkError(err, "Failed to retrieve value");
	std::cout << "Read key: " << key << ", value: " << std::string((const char*)outValue, outValueLen) << std::endl;

	fdb_future_destroy(getFuture);
	fdb_transaction_destroy(transaction);
	fdb_database_destroy(db);
	return 0;
}
