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
  core/PlistTests.cpp
  core/PresetTests.cpp
  core/PresetsTests.cpp
  core/FrameDataTests.cpp
  core/I18nTests.cpp
  core/MenusTests.cpp
  core/MetaManagerTests.cpp
  core/RulesTests.cpp
  core/ParametersTests.cpp
  core/FrameworkExtensionsTests.cpp
  core/KernelTests.cpp
  core/OverlayTests.cpp
  core/WatermarkTests.cpp
  core/LicensingTests.cpp)
target_link_libraries(ofxgrip_core_tests PRIVATE OfxGrip::core GTest::gtest_main)
target_compile_definitions(ofxgrip_core_tests PRIVATE OFXGRIP_KERNEL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kernels")
gtest_discover_tests(ofxgrip_core_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs;OFXGRIP_SUPPORT_DIR=${CMAKE_BINARY_DIR}/test-support;OFXGRIP_CACHE_DIR=${CMAKE_BINARY_DIR}/test-cache")

add_executable(ofxgrip_ofx_tests
  ofx/ActionsTests.cpp
  ofx/PropertySetTests.cpp
  ofx/DescriptorTests.cpp
  ofx/PluginTests.cpp
  ofx/Phase2Tests.cpp
  ofx/InteractTests.cpp
  ofx/LicensingEffectTests.cpp)
target_link_libraries(ofxgrip_ofx_tests PRIVATE OfxGrip::ofxgrip OfxGrip::minihost GTest::gtest_main)
gtest_discover_tests(ofxgrip_ofx_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs;OFXGRIP_SUPPORT_DIR=${CMAKE_BINARY_DIR}/test-support;OFXGRIP_CACHE_DIR=${CMAKE_BINARY_DIR}/test-cache")

add_executable(ofxgrip_integration_tests
  integration/MiniHostTests.cpp
  integration/GainBundleTests.cpp)
target_link_libraries(ofxgrip_integration_tests PRIVATE OfxGrip::minihost OfxGrip::ofxgrip GTest::gtest_main)
get_target_property(_gain_bundle ofxgrip_plugin_OfxGripGain OFXGRIP_BUNDLE_DIR)
get_target_property(_dump_bundle ofxgrip_plugin_OfxGripHostDump OFXGRIP_BUNDLE_DIR)
get_target_property(_spot_bundle ofxgrip_plugin_OfxGripSpot OFXGRIP_BUNDLE_DIR)
target_sources(ofxgrip_integration_tests PRIVATE integration/SpotBundleTests.cpp)
target_compile_definitions(ofxgrip_integration_tests PRIVATE
  OFXGRIP_GAIN_BUNDLE="${_gain_bundle}"
  OFXGRIP_HOSTDUMP_BUNDLE="${_dump_bundle}"
  OFXGRIP_SPOT_BUNDLE="${_spot_bundle}")
add_dependencies(ofxgrip_integration_tests ofxgrip_plugin_OfxGripGain ofxgrip_plugin_OfxGripHostDump ofxgrip_plugin_OfxGripSpot)
if(TARGET ofxgrip_gpu_opencl)
  target_link_libraries(ofxgrip_integration_tests PRIVATE OfxGrip::gpu_opencl)
endif()
if(OFXGRIP_BUILD_RUST)
  target_sources(ofxgrip_integration_tests PRIVATE integration/RustGainBundleTests.cpp)
  target_compile_definitions(ofxgrip_integration_tests PRIVATE OFXGRIP_GAINRS_BUNDLE="${OFXGRIP_RUST_BUNDLE_DIR}")
  add_dependencies(ofxgrip_integration_tests ofxgrip_rust)
endif()
gtest_discover_tests(ofxgrip_integration_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs;OFXGRIP_SUPPORT_DIR=${CMAKE_BINARY_DIR}/test-support;OFXGRIP_CACHE_DIR=${CMAKE_BINARY_DIR}/test-cache")

if(APPLE)
  add_executable(ofxgrip_gpu_tests gpu/MetalBackendTests.cpp)
  target_link_libraries(ofxgrip_gpu_tests PRIVATE OfxGrip::gpu_metal GTest::gtest_main)
  gtest_discover_tests(ofxgrip_gpu_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs;OFXGRIP_SUPPORT_DIR=${CMAKE_BINARY_DIR}/test-support;OFXGRIP_CACHE_DIR=${CMAKE_BINARY_DIR}/test-cache")
endif()

add_executable(ofxgrip_kernel_tests gpu/KernelEngineTests.cpp)
target_link_libraries(ofxgrip_kernel_tests PRIVATE OfxGrip::kernels GTest::gtest_main)
if(APPLE)
  target_link_libraries(ofxgrip_kernel_tests PRIVATE OfxGrip::gpu_metal)
endif()
ofxgrip_add_kernels(TARGET ofxgrip_kernel_tests KERNELS fixtures/kernels/GripBlur.slang fixtures/kernels/GripInspect.slang)
gtest_discover_tests(ofxgrip_kernel_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs;OFXGRIP_SUPPORT_DIR=${CMAKE_BINARY_DIR}/test-support;OFXGRIP_CACHE_DIR=${CMAKE_BINARY_DIR}/test-cache")

if(TARGET ofxgrip_gpu_opencl)
  add_executable(ofxgrip_opencl_tests gpu/OpenCLBackendTests.cpp)
  target_link_libraries(ofxgrip_opencl_tests PRIVATE OfxGrip::gpu_opencl GTest::gtest_main)
  gtest_discover_tests(ofxgrip_opencl_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs;OFXGRIP_SUPPORT_DIR=${CMAKE_BINARY_DIR}/test-support;OFXGRIP_CACHE_DIR=${CMAKE_BINARY_DIR}/test-cache")
endif()

add_executable(ofxgrip_c_tests c/CContractTests.cpp)
target_link_libraries(ofxgrip_c_tests PRIVATE OfxGrip::c OfxGrip::minihost GTest::gtest_main)
gtest_discover_tests(ofxgrip_c_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs;OFXGRIP_SUPPORT_DIR=${CMAKE_BINARY_DIR}/test-support;OFXGRIP_CACHE_DIR=${CMAKE_BINARY_DIR}/test-cache")
