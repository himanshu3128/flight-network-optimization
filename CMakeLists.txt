# CMakeLists.txt -- alternative to the Makefile, for editors and CI that expect
# CMake. Both build systems produce the same two binaries.
#
#   cmake -S . -B build && cmake --build build
#   ctest --test-dir build --output-on-failure

cmake_minimum_required(VERSION 3.10)
project(flightnet VERSION 1.0 LANGUAGES CXX)

# C++14 is deliberate: the project targets older toolchains (it is developed
# against GCC 6), and nothing here needs a later standard.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# --- library ---------------------------------------------------------------

add_library(flightnet_core STATIC
    src/graph.cpp
    src/routing.cpp
    src/maxflow.cpp
    src/capacity.cpp
    src/generator.cpp
    src/loader.cpp
    src/json_output.cpp
    src/benchmark.cpp
)

target_include_directories(flightnet_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(MSVC)
    target_compile_options(flightnet_core PRIVATE /W4)
else()
    target_compile_options(flightnet_core PRIVATE -Wall -Wextra)
endif()

# --- CLI -------------------------------------------------------------------

add_executable(flightnet src/main.cpp)
target_link_libraries(flightnet PRIVATE flightnet_core)

# --- tests -----------------------------------------------------------------

file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(flightnet_tests ${TEST_SOURCES})
target_include_directories(flightnet_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(flightnet_tests PRIVATE flightnet_core)

enable_testing()
add_test(NAME unit_tests COMMAND flightnet_tests)

# A short stress run in CI: enough networks to catch a real regression without
# making the test suite slow. The full 10,000-network run is a manual target.
add_test(NAME stress_smoke
         COMMAND flightnet bench --networks 500 --progress 0)

# The tests write scratch CSV files into a "build" subdirectory relative to the
# working directory, so make sure it exists wherever ctest runs them.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/build)
set_tests_properties(unit_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
