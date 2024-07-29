set(TUTORIAL_SRCS tutorial.actor.cpp)
set(PRAZA_TUTORIAL_SRCS praza_tutorial.actor.cpp)
set (PRAZA_CPP_TUTORIAL_SRCS praza_tutorial.cpp)

add_flow_target(EXECUTABLE NAME tutorial SRCS "${TUTORIAL_SRCS}")
target_link_libraries(tutorial PUBLIC fdbclient)

add_flow_target(EXECUTABLE NAME praza_tutorial SRCS "${PRAZA_TUTORIAL_SRCS}")
target_link_libraries(praza_tutorial PUBLIC fdbclient)

add_flow_target(EXECUTABLE NAME praza_cpp_tutorial SRCS "${PRAZA_CPP_TUTORIAL_SRCS}")
target_link_libraries(praza_cpp_tutorial)