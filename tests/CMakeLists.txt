include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
  URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(INSTALL_GTEST OFF CACHE INTERNAL "")
set(BUILD_GMOCK OFF CACHE INTERNAL "")
set(gtest_force_shared_crt ON CACHE INTERNAL "")
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_executable(ofxgrip_core_tests
  core/TypesTests.cpp
  core/ErrorTests.cpp
  core/ValueTests.cpp
  core/FlagsTests.cpp
  core/ManifestTests.cpp
  core/ManifestJsonTests.cpp
  core/EventsTests.cpp
  core/ExtensionTests.cpp
  core/HostProfileTests.cpp
  core/RenderSnapshotTests.cpp
  core/ImageTests.cpp
  core/CodecTests.cpp
  core/MetaManagerTests.cpp
  core/RulesTests.cpp
  core/ParametersTests.cpp
  core/FrameworkExtensionsTests.cpp)
target_link_libraries(ofxgrip_core_tests PRIVATE OfxGrip::core GTest::gtest_main)
gtest_discover_tests(ofxgrip_core_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs")

add_executable(ofxgrip_ofx_tests
  ofx/ActionsTests.cpp
  ofx/PropertySetTests.cpp
  ofx/DescriptorTests.cpp
  ofx/PluginTests.cpp)
target_link_libraries(ofxgrip_ofx_tests PRIVATE OfxGrip::ofxgrip OfxGrip::minihost GTest::gtest_main)
gtest_discover_tests(ofxgrip_ofx_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs")

add_executable(ofxgrip_integration_tests
  integration/MiniHostTests.cpp
  integration/GainBundleTests.cpp)
target_link_libraries(ofxgrip_integration_tests PRIVATE OfxGrip::minihost OfxGrip::ofxgrip GTest::gtest_main)
get_target_property(_gain_bundle ofxgrip_plugin_OfxGripGain OFXGRIP_BUNDLE_DIR)
get_target_property(_dump_bundle ofxgrip_plugin_OfxGripHostDump OFXGRIP_BUNDLE_DIR)
target_compile_definitions(ofxgrip_integration_tests PRIVATE
  OFXGRIP_GAIN_BUNDLE="${_gain_bundle}"
  OFXGRIP_HOSTDUMP_BUNDLE="${_dump_bundle}")
add_dependencies(ofxgrip_integration_tests ofxgrip_plugin_OfxGripGain ofxgrip_plugin_OfxGripHostDump)
gtest_discover_tests(ofxgrip_integration_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs")

if(APPLE)
  add_executable(ofxgrip_gpu_tests gpu/MetalBackendTests.cpp)
  target_link_libraries(ofxgrip_gpu_tests PRIVATE OfxGrip::gpu_metal GTest::gtest_main)
  gtest_discover_tests(ofxgrip_gpu_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs")
endif()
