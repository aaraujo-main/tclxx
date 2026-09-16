cmake_minimum_required(VERSION 3.16)

project(tclxx VERSION 0.1.2 LANGUAGES CXX)

option(TCLXX_BUILD_DEMOS "Build demo applications" ON)
option(TCLXX_BUILD_TESTS "Build GoogleTest-based test suites" ON)
option(TCLXX_BUILD_BENCHMARKS "Build benchmark applications" OFF)

add_subdirectory(include/tclxx)

if(TCLXX_BUILD_DEMOS)
    add_subdirectory(demo/point)
endif()

if(TCLXX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(TCLXX_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()
