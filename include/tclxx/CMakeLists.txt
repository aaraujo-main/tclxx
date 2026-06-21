cmake_minimum_required(VERSION 3.16)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(tclxx INTERFACE)
add_library(tclxx::tclxx ALIAS tclxx)

target_include_directories(tclxx
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/..>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(tclxx INTERFACE cxx_std_17)

install(
    DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tclxx
    FILES_MATCHING
        PATTERN "*.hpp"
)

install(
    FILES ${CMAKE_CURRENT_LIST_DIR}/../tclxx.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    TARGETS tclxx
    EXPORT tclxxTargets
)

install(
    EXPORT tclxxTargets
    FILE tclxxTargets.cmake
    NAMESPACE tclxx::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tclxx
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/tclxxConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_LIST_DIR}/cmake/tclxxConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/tclxxConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tclxx
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/tclxxConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/tclxxConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tclxx
)
