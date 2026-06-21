cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(tclxx_tests LANGUAGES CXX)
endif()

include(FetchContent)
include(GoogleTest)

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    FetchContent_Declare(
        googletest
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    )
    # Prevent GTest and GMock from being installed alongside tclxx
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(INSTALL_GMOCK OFF CACHE BOOL "" FORCE)
    # Keep parent project settings untouched when gtest is vendored.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

find_package(TCL QUIET)

if(NOT TCL_LIBRARY OR NOT TCL_INCLUDE_PATH)
    find_path(TCLXX_TCL_INCLUDE_DIR tcl.h REQUIRED)
    find_library(TCLXX_TCL_LIBRARY NAMES tcl tcl8.7 tcl8.6 REQUIRED)
endif()

function(tclxx_link_tcl target)
    if(TARGET Tcl::Tcl)
        target_link_libraries(${target} PRIVATE Tcl::Tcl)
    elseif(TARGET TCL::TCL)
        target_link_libraries(${target} PRIVATE TCL::TCL)
    else()
        if(TCL_INCLUDE_PATH AND TCL_LIBRARY)
            target_include_directories(${target} PRIVATE ${TCL_INCLUDE_PATH})
            target_link_libraries(${target} PRIVATE ${TCL_LIBRARY})
        else()
            target_include_directories(${target} PRIVATE ${TCLXX_TCL_INCLUDE_DIR})
            target_link_libraries(${target} PRIVATE ${TCLXX_TCL_LIBRARY})
        endif()
    endif()
endfunction()

set(TCLXX_TEST_SOURCES
    obj_type_test.cpp
    obj_cast_test.cpp
    cmd_test.cpp
)

foreach(src IN LISTS TCLXX_TEST_SOURCES)
    get_filename_component(test_name ${src} NAME_WE)
    add_executable(${test_name} ${src})
    target_compile_features(${test_name} PRIVATE cxx_std_17)

    if(TARGET tclxx::tclxx)
        target_link_libraries(${test_name} PRIVATE tclxx::tclxx)
    else()
        target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
    endif()

    tclxx_link_tcl(${test_name})
    target_link_libraries(${test_name} PRIVATE GTest::gtest_main)

    gtest_discover_tests(
        ${test_name}
        DISCOVERY_TIMEOUT 30
        PROPERTIES
            LABELS "unit;tclxx"
    )
endforeach()
