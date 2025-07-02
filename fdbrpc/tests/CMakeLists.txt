if(WITH_GRPC)
  generate_grpc_protobuf(fdbrpc.test protos/echo.proto)
endif()
