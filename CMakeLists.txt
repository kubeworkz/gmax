cmake_minimum_required(VERSION 3.20)

project(gmax
  VERSION 0.1.0
  DESCRIPTION "DFT/ATPG platform for grx930 and G100"
  LANGUAGES CXX)

# ADR-001: C++20, no compiler extensions.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Architecture 19.5 documents ./build/gmax, so binaries go next to the build root
# instead of into a subdirectory per target.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)

# G0.4 makes the warning gate a merge gate, so it is on by default and can only
# be relaxed on purpose.
option(GMAX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
