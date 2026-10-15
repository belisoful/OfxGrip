cmake_minimum_required(VERSION 3.24)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" OFXGRIP_VERSION_TEXT)
string(STRIP "${OFXGRIP_VERSION_TEXT}" OFXGRIP_VERSION_TEXT)

project(OfxGrip
  VERSION ${OFXGRIP_VERSION_TEXT}
  DESCRIPTION "A C++20 framework for OpenFX image-effect plug-ins"
  LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(OFXGRIP_BUILD_TESTS "Build the test suite" ON)
option(OFXGRIP_BUILD_PLUGINS "Build the probe and sample plug-ins" ON)
option(OFXGRIP_BUILD_MINIHOST "Build the headless mini host" ON)
option(OFXGRIP_BUILD_RUST "Build the Rust crates and the Rust sample bundle with cargo" OFF)
option(OFXGRIP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(OFXGRIP_COVERAGE "Instrument for source-based coverage (clang)" OFF)
set(OFXGRIP_SANITIZER "" CACHE STRING "Sanitizers to enable: address, thread, undefined (semicolon separated)")
set(OFXGRIP_COVERAGE_MIN "85" CACHE STRING "Minimum line coverage percentage the coverage script enforces")
set(OFXGRIP_CODESIGN_IDENTITY "" CACHE STRING "codesign identity for bundles on macOS; empty signs ad hoc")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.5" CACHE STRING "Minimum macOS version" FORCE)
endif()

include(OfxGripCompiler)
include(OfxGripPlugin)
include(OfxGripKernels)

add_subdirectory(src)

if(OFXGRIP_BUILD_MINIHOST)
  add_subdirectory(minihost)
endif()

if(OFXGRIP_BUILD_PLUGINS)
  add_subdirectory(plugins)
endif()

if(OFXGRIP_BUILD_RUST)
  include(OfxGripRust)
endif()

if(OFXGRIP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
