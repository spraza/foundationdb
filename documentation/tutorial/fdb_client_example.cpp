// clang++ -std=c++11 -L/root/build_output/lib -lfdb_c fdb_client_example.cpp -o fdb_client_example

// #define FDB_API_VERSION 740
// #include "/root/src/foundationdb/bindings/c/foundationdb/fdb_c.h"

#define FDB_API_VERSION 630
#include <foundationdb/fdb_c.h>

#include <iostream>
#include <cstring>

#define CLUSTER_PATH "/tmp/loopback-cluster/loopback-cluster/fdb.cluster"

void checkError(fdb_error_t err, const char* msg) {
	if (err) {
		std::cerr << msg << ": " << fdb_get_error(err) << std::endl;
		exit(1);
	}
}

int main() {
	// Initialize the FDB client API
	fdb_error_t err = fdb_select_api_version(FDB_API_VERSION);
	checkError(err, "Error selecting API version");

	// Open the default FDB database
	FDBDatabase* db;
	err = fdb_create_database(CLUSTER_PATH, &db);
	checkError(err, "Error creating database");

	// Create a transaction
	FDBTransaction* tx;
	err = fdb_database_create_transaction(db, &tx);
	checkError(err, "Error creating transaction");

	// Define key and value
	const char* key = "myKey";
	const char* value = "myValue";

	// Set the key-value pair
	fdb_transaction_set(tx, (const uint8_t*)key, std::strlen(key), (const uint8_t*)value, std::strlen(value));

	// Commit the transaction and get the future
	FDBFuture* commit_future = fdb_transaction_commit(tx);

	// Wait for the future to be ready
	err = fdb_future_block_until_ready(commit_future);
	checkError(err, "Error waiting for commit future");

	// Check if there was an error during commit
	err = fdb_future_get_error(commit_future);
	checkError(err, "Error committing transaction");

	// Clean up the commit future
	fdb_future_destroy(commit_future);

	// Destroy the transaction after commit
	fdb_transaction_destroy(tx);

	// Create a new transaction to read the value
	err = fdb_database_create_transaction(db, &tx);
	checkError(err, "Error creating transaction for read");

	// Get the value for the key
	FDBFuture* future = fdb_transaction_get(tx, (const uint8_t*)key, std::strlen(key), /*snapshot=*/0);

	// Wait for the future to be ready
	err = fdb_future_block_until_ready(future);
	checkError(err, "Error waiting for future");

	// Retrieve the result
	const uint8_t* value_out;
	int value_length;
	err = fdb_future_get_value(future, nullptr, &value_out, &value_length);
	checkError(err, "Error getting value from future");

	// Print the result
	std::cout << "Key: " << key << ", Value: " << std::string((const char*)value_out, value_length) << std::endl;

	// Clean up
	fdb_future_destroy(future);
	fdb_transaction_destroy(tx);
	fdb_database_destroy(db);

	return 0;
}
