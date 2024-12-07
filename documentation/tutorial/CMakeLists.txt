set(TUTORIAL_SRCS tutorial.actor.cpp)

add_flow_target(EXECUTABLE NAME tutorial SRCS "${TUTORIAL_SRCS}")
target_link_libraries(tutorial PUBLIC fdbclient)

# RocksDB
set(PRAZA_ROCKSDB_TUTORIAL_SRCS praza_rocksdb.cpp)
if(NOT TARGET rocksdb)
    include(CompileRocksDB)
else()
  ExternalProject_Get_Property(rocksdb BINARY_DIR)
  set(ROCKSDB_LIBRARIES
      ${BINARY_DIR}/librocksdb.a)
  ExternalProject_Get_Property(rocksdb SOURCE_DIR)
  set(ROCKSDB_INCLUDE_DIR "${SOURCE_DIR}/include")
endif()
add_flow_target(EXECUTABLE NAME praza_rocksdb_tutorial SRCS "${PRAZA_ROCKSDB_TUTORIAL_SRCS}")
target_include_directories(praza_rocksdb_tutorial PUBLIC "${CMAKE_SOURCE_DIR}/contrib/fmt-8.1.1/include")
target_link_libraries(praza_rocksdb_tutorial PUBLIC fmt::fmt)
add_dependencies(praza_rocksdb_tutorial rocksdb)
target_include_directories(praza_rocksdb_tutorial PUBLIC ${ROCKSDB_INCLUDE_DIR})
target_link_libraries(praza_rocksdb_tutorial PUBLIC ${ROCKSDB_LIBRARIES} ${lz4_STATIC_LIBRARIES})

# FDB Client Example
add_executable(praza_fdb_client_example praza_fdb_client.cpp)
target_link_libraries(praza_fdb_client_example PRIVATE fdb_c)