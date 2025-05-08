#include "fdbclient/StorageCheckpoint.h"

void CheckpointMetaData::setSerializedCheckpoint(Standalone<StringRef> tmpObj) {
	// 1) figure out how big the payload must be (round up to next 5 k)
	size_t objSize = tmpObj.size();
	size_t targetSize = std::max<size_t>(5000, ((objSize + 4999) / 5000) * 5000);
	size_t paddingBytes = targetSize - objSize;

	// 2) build payload + padding + 100-byte footer
	std::string payload = tmpObj.toString();
	std::string padding(paddingBytes, 'x');

	// footer: 100 bytes, starting with ASCII decimal of paddingBytes
	std::string footer(100, 'x');
	std::string num = std::to_string(paddingBytes);
	ASSERT(num.size() <= footer.size());
	std::memcpy(&footer[0], num.data(), num.size());
	ASSERT(footer.size() == 100);

	// 3) stash it
	serializedCheckpoint = std::move(payload) + padding + footer;
	//TraceEvent("Baz").detail("SerSet", serializedCheckpoint);
}

Standalone<StringRef> CheckpointMetaData::getSerializedCheckpoint() const {
	const std::string& str = serializedCheckpoint;
	ASSERT(str.size() >= 100);

	// 1) pull off footer and parse decimal prefix
	constexpr size_t footerSize = 100;
	size_t start = str.size() - footerSize;
	size_t paddingBytes = 0;
	for (size_t i = start; i < str.size() && std::isdigit(str[i]); ++i) {
		paddingBytes = paddingBytes * 10 + (str[i] - '0');
	}

	// 2) compute payload length
	size_t payloadSize = str.size() - paddingBytes - footerSize;
	ASSERT(payloadSize <= str.size());

	// 3) reinterpret_cast data pointer + cast length to int
	auto ptr = reinterpret_cast<const uint8_t*>(str.data());
	StringRef ref(ptr, int(payloadSize));

	// 4) wrap in a Standalone so it allocates in its own Arena
	auto ret = Standalone<StringRef>(ref);
	//TraceEvent("Baz").detail("SerGet", ret);
	return ret;
}

// void CheckpointMetaData::setSerializedCheckpoint(Standalone<StringRef> tmpObj) {
// 	std::string tmp = tmpObj.toString();
// 	serializedCheckpoint = tmp;
// 	TraceEvent("Baz").detail("SerSet", serializedCheckpoint);
// }

// Standalone<StringRef> CheckpointMetaData::getSerializedCheckpoint() const {
// 	std::string x = this->serializedCheckpoint;
// 	auto ret = StringRef(x);
// 	TraceEvent("Baz").detail("SerGet", ret);
// 	return ret;
// }