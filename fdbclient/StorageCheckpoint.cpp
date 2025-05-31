#include "fdbclient/StorageCheckpoint.h"
#include "flow/Buggify.h"

namespace {
constexpr size_t PAYLOAD_ROUND_TO_NEXT = 5000;
constexpr size_t FOOTER_BYTE_SIZE = 100;
} // namespace

// TOOD: polish this comment
// ONLY in simulation or unitttests role, adds dynamic padding to the checkpoint payload
// Very simple protocol:
// Intentionally optimized for readability and *not* performance e.g. encoding the padding bytes in the footer
// is literally done using std::to_string, and very byte-space wasteful. But in simulation and testing,
// we'd rather have readability than a compact binary encoding.
void CheckpointMetaData::setSerializedCheckpoint(Standalone<StringRef> checkpoint) {
	const bool addPadding = g_network->isSimulated();
	if (!addPadding) {
		serializedCheckpoint = checkpoint;
		return;
	}

	// 1) Determine target size for payload and how much padding must be added to reach that target
	const size_t payloadSize = checkpoint.size();
	const size_t targetSize =
	    std::max<size_t>(PAYLOAD_ROUND_TO_NEXT,
	                     ((payloadSize + (PAYLOAD_ROUND_TO_NEXT - 1)) / PAYLOAD_ROUND_TO_NEXT) * PAYLOAD_ROUND_TO_NEXT);
	const size_t paddingBytes = targetSize - payloadSize;

	// 2) Build the footer. FOOTER_BYTE_SIZE bytes, starting with ASCII decimal of paddingBytes
	std::string footer(FOOTER_BYTE_SIZE, 'f');
	const std::string num = std::to_string(paddingBytes);
	ASSERT(num.size() <= footer.size());
	std::memcpy(&footer[0], num.data(), num.size());
	ASSERT(footer.size() == FOOTER_BYTE_SIZE);

	// 3) Build the final serialized checkpoint: payload + dynamic padding + footer
	std::string payload = checkpoint.toString();
	std::string padding(paddingBytes, 'p');
	serializedCheckpoint = std::move(payload) + padding + footer;

	// For debugging (uncomment if needed)
	TraceEvent("CheckpointSet")
	    .detail("OriginalCheckpoint", checkpoint)
	    .detail("OriginalCheckpointSize", checkpoint.size())
	    .detail("SerializedCheckpoint", serializedCheckpoint)
	    .detail("SerializedCheckpointSize", serializedCheckpoint.size())
	    .detail("Footer", footer)
	    .detail("FooterSize", FOOTER_BYTE_SIZE)
	    .detail("PaddingSize", paddingBytes);
}

Standalone<StringRef> CheckpointMetaData::getSerializedCheckpoint() const {
	const bool addPadding = g_network->isSimulated();
	if (!addPadding) {
		return serializedCheckpoint;
	}

	// 1) Extract footer and padding size
	const std::string& str = serializedCheckpoint.toString();
	ASSERT(str.size() >= FOOTER_BYTE_SIZE);
	size_t start = str.size() - FOOTER_BYTE_SIZE;
	size_t paddingBytes = 0;
	for (size_t i = start; i < str.size() && std::isdigit(str[i]); ++i) {
		paddingBytes = paddingBytes * 10 + (str[i] - '0');
	}

	// 2) Compute payload size
	size_t payloadSize = str.size() - paddingBytes - FOOTER_BYTE_SIZE;
	ASSERT(payloadSize <= str.size());

	// 3) Compute payload to return
	auto ptr = reinterpret_cast<const uint8_t*>(str.data());
	StringRef ref(ptr, int(payloadSize));
	auto ret = Standalone<StringRef>(ref);

	TraceEvent("CheckpointGet")
	    .detail("ReturnedCheckpoint", ret)
	    .detail("ReturnedCheckpointSize", ret.size())
	    .detail("SerializedCheckpoint", serializedCheckpoint)
	    .detail("SerializedCheckpointSize", serializedCheckpoint.size())
	    .detail("FooterSize", FOOTER_BYTE_SIZE)
	    .detail("PaddingSize", paddingBytes);

	return ret;
}
