cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(tclxx_benchmark LANGUAGES CXX)
endif()

find_package(TCL QUIET)

if(NOT TCL_LIBRARY OR NOT TCL_INCLUDE_PATH)
    # Search common TCL installation locations
    find_path(TCLXX_TCL_INCLUDE_DIR tcl.h
        PATHS /usr/include/tcl8.7 /usr/include/tcl8.6 /usr/include
        NO_DEFAULT_PATH
    )
    find_library(TCLXX_TCL_LIBRARY
        NAMES tcl8.7 tcl8.6 tcl
        PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib
        NO_DEFAULT_PATH
    )
    if(NOT TCLXX_TCL_INCLUDE_DIR OR NOT TCLXX_TCL_LIBRARY)
        message(FATAL_ERROR "TCL development files not found. Install tcl-dev or libtcl-dev.")
    endif()
    set(TCL_INCLUDE_PATH "${TCLXX_TCL_INCLUDE_DIR}")
    set(TCL_LIBRARY "${TCLXX_TCL_LIBRARY}")
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

add_executable(tclxx_benchmark perf_benchmark.cpp)
target_compile_features(tclxx_benchmark PRIVATE cxx_std_17)
target_compile_options(tclxx_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/O2>
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3>
)

if(TARGET tclxx::tclxx)
    target_link_libraries(tclxx_benchmark PRIVATE tclxx::tclxx)
else()
    target_include_directories(tclxx_benchmark PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
endif()

tclxx_link_tcl(tclxx_benchmark)
