# FDB Trace Scrubber - Go-based TUI tool
cmake_minimum_required(VERSION 3.13)

# Find Go
find_program(GO_EXECUTABLE go)
if(NOT GO_EXECUTABLE)
    message(FATAL_ERROR "Go not found. Please install Go 1.21 or later.")
endif()

# Set output directory for the binary
set(SCRUBBER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
set(SCRUBBER_BINARY "${SCRUBBER_OUTPUT_DIR}/scrubber")

# Ensure output directory exists
file(MAKE_DIRECTORY ${SCRUBBER_OUTPUT_DIR})

# Custom target to build the scrubber
add_custom_target(scrubber ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Tidying Go dependencies..."
    COMMAND ${GO_EXECUTABLE} mod tidy
    COMMAND ${CMAKE_COMMAND} -E echo "Building scrubber..."
    COMMAND ${GO_EXECUTABLE} build -o ${SCRUBBER_BINARY}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building FDB trace scrubber"
    BYPRODUCTS ${SCRUBBER_BINARY}
)

# Make sure the binary is executable
add_custom_command(TARGET scrubber POST_BUILD
    COMMAND chmod +x ${SCRUBBER_BINARY}
    COMMENT "Making scrubber executable"
)

message(STATUS "Scrubber will be built to: ${SCRUBBER_BINARY}")
