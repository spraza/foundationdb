#include "fdbrpc/FlowTransport.h"
#include "flow/Buggify.h"
#include "flow/TaskPriority.h"
#include "fmt/format.h"
#include "flow/flow.h"
#include "flow/Platform.h"
#include "flow/Arena.h"
#include "fdbclient/NativeAPI.actor.h"
#include "fdbclient/ReadYourWrites.h"
#include "flow/TLSConfig.actor.h"
#include <functional>
#include <new>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <iostream>

#include "flow/actorcompiler.h"

NetworkAddress serverAddress = NetworkAddress::parse("127.0.0.1:6666");

enum TutorialWellKnownEndpoints {
	WLTOKEN_COUNT_SERVER = WLTOKEN_FIRST_AVAILABLE,
};

struct CountServerIf {
	constexpr static FileIdentifier file_identifier = 3152015;

	RequestStream<struct GetInterfaceRequest> getInterface;
	RequestStream<struct AddRequest> add;
	RequestStream<struct SubtractRequest> subtract;
	RequestStream<struct GetRequest> get;

	template <typename T>
	void serialize(T& t) {
		serializer(t, getInterface, add, subtract, get);
	}
};

struct GetInterfaceRequest {
	constexpr static FileIdentifier file_identifier = 12004156;

	ReplyPromise<CountServerIf> reply;

	template <typename T>
	void serialize(T& t) {
		serializer(t, reply);
	}
};

struct AddRequest {
	constexpr static FileIdentifier file_identifier = 123;

	int val;
	ReplyPromise<Void> reply;

	template <typename T>
	void serialize(T& t) {
		serializer(t, reply);
	}
};

struct SubtractRequest {
	constexpr static FileIdentifier file_identifier = 124;

	int val;
	ReplyPromise<Void> reply;

	template <typename T>
	void serialize(T& t) {
		serializer(t, reply);
	}
};

struct StreamReply : ReplyPromiseStreamReply {
	constexpr static FileIdentifier file_identifier = 440804;

	int index = 0;
	StreamReply() = default;
	explicit StreamReply(int index) : index(index) {}

	size_t expectedSize() const { return 2e6; }

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, ReplyPromiseStreamReply::acknowledgeToken, ReplyPromiseStreamReply::sequence, index);
	}
};

struct GetRequest {
	constexpr static FileIdentifier file_identifier = 125;

	ReplyPromiseStream<StreamReply> reply;

	template <typename T>
	void serialize(T& t) {
		serializer(t, reply);
	}
};

ACTOR Future<Void> countServer() {
	state CountServerIf csi;
	csi.getInterface.makeWellKnownEndpoint(WLTOKEN_COUNT_SERVER, TaskPriority::DefaultEndpoint);
	state int count = 0;
	loop {
		choose {
			when(GetInterfaceRequest clientReq = waitNext(csi.getInterface.getFuture())) {
				clientReq.reply.send(csi);
			}
			when(AddRequest clientReq = waitNext(csi.add.getFuture())) {
				count += clientReq.val;
			}
			when(SubtractRequest clientReq = waitNext(csi.subtract.getFuture())) {
				count -= clientReq.val;
			}
			when(GetRequest clientReq = waitNext(csi.get.getFuture())) {
				clientReq.reply.send(StreamReply(count));
			}
		}
	}
	// return Void();
}

ACTOR Future<Void> countClient() {
	state CountServerIf csi;
	csi.getInterface =
	    RequestStream<GetInterfaceRequest>(Endpoint::wellKnown({ .address = serverAddress }, WLTOKEN_COUNT_SERVER));
	CountServerIf rsp = wait(csi.getInterface.getReply(GetInterfaceRequest{}));
	csi = rsp;

	csi.add.send(10);
	csi.subtract.send(3);

	GetRequest getRequest;
	int value = wait(csi.get.getReply(getRequest));
	std::cout << value << std::endl;

	return Void();
}

ACTOR Future<Void> start() {
	wait(countServer());
	wait(countClient());
	return Void();
}

int main(int argc, char* argv[]) {
	platformInit();
	g_network = newNet2(TLSConfig(), false, true);

	// RPC setup -- start
	const bool isServer{ true };
	FlowTransport::createInstance(!isServer, 0, WLTOKEN_COUNT_SERVER);
	NetworkAddress publicAddress = NetworkAddress::parse("0.0.0.0:0");
	if (isServer) {
		publicAddress = NetworkAddress::parse("0.0.0.0:6666");
	}
	try {
		if (isServer) {
			auto listenError = FlowTransport::transport().bind(publicAddress, publicAddress);
			if (listenError.isError()) {
				listenError.get();
			}
		}
	} catch (Error& e) {
		std::cout << format("Error while binding to address (%d): %s\n", e.code(), e.what());
	}
	// RPC setup -- end

	auto x = stopAfter(start());
	g_network->run();
	return 0;
}
