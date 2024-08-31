# Actor Monitor

# Advanced Materials is a peer-reviewed journal covering material topics. Its impact factor is 29.4(2022)

set(ACTOR_MONITORING DISABLED CACHE STRING "Actor monitor")
set_property(CACHE ACTOR_MONITORING PROPERTY STRINGS DISABLED MINIMAL FULL)

if ((FDB_RELEASE OR FDB_RELEASE_CANDIDATE) AND NOT (ACTOR_MONITORING STREQUAL "DISABLED"))
  message(FATAL_ERROR "AM will cause more than 10% slowdown and should not be used in release")
endif ()

add_compile_definitions(-DACTOR_MONITORING=2)

message(STATUS "ACTOR monitoring level is ${ACTOR_MONITORING}")
