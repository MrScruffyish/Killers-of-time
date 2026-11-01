cmake_minimum_required(VERSION 3.21)
project(KillersOfTime VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network Widgets Test)

option(KOT_BUILD_TESTS "Build unit tests" ON)

# Shared warning flags, linked by every target.
add_library(kot_warnings INTERFACE)
target_compile_options(kot_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)

# All code includes headers relative to src/, e.g. #include "core/util/Rng.h"
set(KOT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_subdirectory(src)

if(KOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
