set(TUTORIAL_SRCS tutorial.actor.cpp)
set(PRAZA_TUTORIAL_SRCS praza_tutorial.actor.cpp)
set(PRAZA_CPP_TUTORIAL_SRCS praza_tutorial.cpp)
set(PRAZA_ROCKSDB_TUTORIAL_SRCS praza_rocksdb.cpp)

add_flow_target(EXECUTABLE NAME tutorial SRCS "${TUTORIAL_SRCS}")
target_link_libraries(tutorial PUBLIC fdbclient)

add_flow_target(EXECUTABLE NAME praza_tutorial SRCS "${PRAZA_TUTORIAL_SRCS}")

if(USE_JEMALLOC)
  target_include_directories(praza_tutorial INTERFACE $<TARGET_PROPERTY:jemalloc::jemalloc, INTERFACE_INCLUDE_DIRECTORIES>)  
  target_link_libraries(praza_tutorial PRIVATE jemalloc::jemalloc PUBLIC fdbclient)
else()
  add_flow_target(EXECUTABLE NAME praza_tutorial SRCS "${PRAZA_TUTORIAL_SRCS}")
  target_link_libraries(praza_tutorial PUBLIC fdbclient)
endif()

add_flow_target(EXECUTABLE NAME praza_cpp_tutorial SRCS "${PRAZA_CPP_TUTORIAL_SRCS}")
target_link_libraries(praza_cpp_tutorial)

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

