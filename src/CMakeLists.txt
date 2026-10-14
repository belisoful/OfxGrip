include(FetchContent)

FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# ---- ofxgrip_core: host-neutral, no OpenFX include --------------------------------------------

add_library(ofxgrip_core STATIC
  core/Types.cpp
  core/Error.cpp
  core/Value.cpp
  core/Flags.cpp
  core/Manifest.cpp
  core/ManifestJson.cpp
  core/Events.cpp
  core/Extension.cpp
  core/HostServices.cpp
  core/Quirks.cpp
  core/HostProfile.cpp
  core/RenderSnapshot.cpp
  core/Image.cpp)
add_library(OfxGrip::core ALIAS ofxgrip_core)
target_include_directories(ofxgrip_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(ofxgrip_core PUBLIC OfxGrip::options PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(ofxgrip_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(ofxgrip_core PUBLIC Threads::Threads)

# ---- ofxgrip: the OpenFX adapter ---------------------------------------------------------------

add_library(ofxgrip_openfx_headers INTERFACE)
add_library(OfxGrip::openfx_headers ALIAS ofxgrip_openfx_headers)
target_include_directories(ofxgrip_openfx_headers INTERFACE "${PROJECT_SOURCE_DIR}/external/openfx/include")

add_library(ofxgrip STATIC
  ofx/Suites.cpp
  ofx/PropertySet.cpp
  ofx/Actions.cpp
  ofx/Descriptor.cpp
  ofx/OfxHostServices.cpp
  ofx/Effect.cpp
  ofx/Plugin.cpp)
add_library(OfxGrip::ofxgrip ALIAS ofxgrip)
target_link_libraries(ofxgrip PUBLIC OfxGrip::core OfxGrip::openfx_headers)
if(APPLE)
  target_link_libraries(ofxgrip PUBLIC "-framework CoreFoundation")
endif()

# ---- metal-cpp: Apple's C++ Metal interface, vendored, for the phase-1 Metal backend --------------

if(APPLE)
  add_library(ofxgrip_metal_cpp INTERFACE)
  add_library(OfxGrip::metal_cpp ALIAS ofxgrip_metal_cpp)
  target_include_directories(ofxgrip_metal_cpp INTERFACE "${PROJECT_SOURCE_DIR}/external/metal-cpp")
  target_link_libraries(ofxgrip_metal_cpp INTERFACE "-framework Metal" "-framework Foundation" "-framework QuartzCore")
endif()
