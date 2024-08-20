#include "fdbrpc/FlowTransport.h"
#include "fdbrpc/fdbrpc.h"
#include "flow/Buggify.h"
#include "flow/TaskPriority.h"
#include "flow/genericactors.actor.h"
#include "fmt/format.h"
#include "flow/flow.h"
#include "flow/Platform.h"
#include "flow/Arena.h"
#include "fdbclient/NativeAPI.actor.h"
#include "fdbclient/ReadYourWrites.h"
#include "flow/TLSConfig.actor.h"
#include <cstdint>
#include <functional>
#include <new>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <iostream>

#include "flow/actorcompiler.h"

NetworkAddress serverAddress;
enum TutorialWellKnownEndpoints { WLTOKEN_KV_SERVER = WLTOKEN_FIRST_AVAILABLE, WLTOKEN_COUNT_IN_TUTORIAL };
bool isServer = false;

// Owned by server
// Server can serialize and send it to client via getInterface RPC
struct KVInterface {
	constexpr static FileIdentifier file_identifier = 3152015;

	RequestStream<struct ConnectRequest> connectRequest;
	RequestStream<struct SetRequest> setRequest;
	RequestStream<struct GetRequest> getRequest;

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, connectRequest, setRequest, getRequest);
	}
};

struct ConnectRequest {
	constexpr static FileIdentifier file_identifier = 3152016;

	ReplyPromise<KVInterface> reply;

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, reply);
	}
};

struct SetRequest {
	constexpr static FileIdentifier file_identifier = 3152017;

	std::string key;
	std::string val;
	ReplyPromise<Void> reply;

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, key, val, reply);
	}
};

struct GetRequest {
	constexpr static FileIdentifier file_identifier = 3152018;

	std::string key;
	ReplyPromise<std::string> reply;

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, key, reply);
	}
};

ACTOR Future<Void> server() {
	state KVInterface ifx;
	state std::unordered_map<std::string, std::string> store;
	ifx.connectRequest.makeWellKnownEndpoint(WLTOKEN_KV_SERVER, TaskPriority::DefaultEndpoint);
	loop {
		choose {
			when(ConnectRequest req = waitNext(ifx.connectRequest.getFuture())) {
				std::cout << "Received connection attempt\n";
				req.reply.send(ifx);
			}
			when(GetRequest req = waitNext(ifx.getRequest.getFuture())) {
				auto iter = store.find(req.key);
				if (iter == store.end()) {
					req.reply.sendError(io_error());
				} else {
					req.reply.send(iter->second);
				}
			}
			when(SetRequest req = waitNext(ifx.setRequest.getFuture())) {
				store[req.key] = req.val;
			}
		}
	}
	// return Void();
}

ACTOR Future<KVInterface> connect() {
	// TODO: can I avoid creating ifx like this and use RequestStream variable directly?
	KVInterface ifx;
	ifx.connectRequest =
	    RequestStream<ConnectRequest>(Endpoint::wellKnown({ .address = serverAddress }, WLTOKEN_KV_SERVER));
	KVInterface result_ifx = wait(ifx.connectRequest.getReply(ConnectRequest()));
	return result_ifx;
}

ACTOR Future<Void> client() {
	state KVInterface ifx = wait(connect());
	wait(ifx.setRequest.getReply(SetRequest{ .key = "foo", .val = "bar" }));
	state std::string val = wait(ifx.getRequest.getReply(GetRequest{ .key = "foo" }));
	std::cout << val << std::endl;
	return Void();
}

ACTOR Future<Void> start() {
	std::vector<Future<Void>> all;
	if (isServer) {
		all.push_back(server());
	} else {
		all.push_back(client());
	}
	wait(waitForAll(all));
	return Void();
}

void setup_flow_server_rpc() {
	const std::string port{ "6666" };
	FlowTransport::createInstance(false, 0, WLTOKEN_COUNT_IN_TUTORIAL);
	NetworkAddress publicAddress = NetworkAddress::parse("0.0.0.0:" + port);
	try {
		auto listenError = FlowTransport::transport().bind(publicAddress, publicAddress);
		if (listenError.isError()) {
			listenError.get();
		}
	} catch (Error& e) {
		std::cout << format("Error while binding to address (%d): %s\n", e.code(), e.what());
	}
}

void setup_flow_client_rpc() {
	FlowTransport::createInstance(true, 0, WLTOKEN_COUNT_IN_TUTORIAL);
}

int main(int argc, char* argv[]) {
	for (int i = 1; i < argc; ++i) {
		std::string arg(argv[i]);
		if (arg == "--is-server") {
			isServer = true;
		}
	}
	std::cout << "isServer = " << isServer << std::endl;
	platformInit();
	g_network = newNet2(TLSConfig(), false, true);
	if (isServer) {
		setup_flow_server_rpc();
	} else {
		setup_flow_client_rpc();
	}
	auto x = stopAfter(start());
	g_network->run();
	return 0;
}
