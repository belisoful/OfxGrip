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
  core/Codec.cpp
  core/Flags.cpp
  core/Manifest.cpp
  core/ManifestJson.cpp
  core/Events.cpp
  core/Extension.cpp
  core/HostServices.cpp
  core/Quirks.cpp
  core/HostProfile.cpp
  core/RenderSnapshot.cpp
  core/Image.cpp
  core/Plist.cpp
  core/Preset.cpp
  core/Presets.cpp
  core/About.cpp
  core/Debug.cpp
  core/FrameData.cpp
  core/AnalysisExtension.cpp
  core/I18n.cpp
  core/InstanceTracker.cpp
  core/Regression.cpp
  core/MetaManager.cpp
  core/Rules.cpp
  core/Parameters.cpp
  core/MetaExtension.cpp
  core/VisibilityExtension.cpp
  core/VersioningExtension.cpp
  core/KernelLayout.cpp
  core/KernelMetadata.cpp
  core/KernelTile.cpp
  core/KernelValues.cpp
  core/KernelCatalog.cpp
  core/Overlay.cpp
  core/Digest.cpp
  core/Watermark.cpp
  core/Licensing.cpp
  core/LicensingProviders.cpp)
add_library(OfxGrip::core ALIAS ofxgrip_core)
target_include_directories(ofxgrip_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(ofxgrip_core PUBLIC OfxGrip::options PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(ofxgrip_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(ofxgrip_core PUBLIC Threads::Threads)

# ---- metal-cpp: Apple's C++ Metal interface, vendored ---------------------------------------------

if(APPLE)
  add_library(ofxgrip_metal_cpp INTERFACE)
  add_library(OfxGrip::metal_cpp ALIAS ofxgrip_metal_cpp)
  target_include_directories(ofxgrip_metal_cpp SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/external/metal-cpp")
  target_link_libraries(ofxgrip_metal_cpp INTERFACE "-framework Metal" "-framework Foundation")
endif()

# ---- ofxgrip_gpu_metal: the Metal backend over metal-cpp -----------------------------------------
# One static library holds metal-cpp's private implementation, so a binary links it once however
# many of its parts (adapter, mini host, plug-in) use Metal.

if(APPLE)
  add_library(ofxgrip_gpu_metal STATIC
    gpu/MetalBackend.cpp
    gpu/MetalWatermark.cpp
    gpu/MetalCppImpl.cpp)
  add_library(OfxGrip::gpu_metal ALIAS ofxgrip_gpu_metal)
  target_link_libraries(ofxgrip_gpu_metal PUBLIC OfxGrip::core PRIVATE OfxGrip::metal_cpp)
  target_compile_definitions(ofxgrip_gpu_metal PUBLIC OFXGRIP_HAS_METAL=1)
endif()

# ---- ofxgrip_kernels: the kernel engine and the CPU backend over the Slang C++ prelude -------------

add_library(ofxgrip_kernels STATIC
  gpu/KernelEngine.cpp
  gpu/CpuKernelBackend.cpp
  gpu/KernelCpu.cpp)
add_library(OfxGrip::kernels ALIAS ofxgrip_kernels)
target_link_libraries(ofxgrip_kernels PUBLIC OfxGrip::core)
target_include_directories(ofxgrip_kernels SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/external/slang-prelude")

if(APPLE)
  target_sources(ofxgrip_gpu_metal PRIVATE gpu/MetalKernelBackend.cpp)
  target_link_libraries(ofxgrip_gpu_metal PUBLIC OfxGrip::kernels)
endif()

# ---- ofxgrip_gpu_opencl: the OpenCL backend for native kernels ---------------------------------------

if(APPLE)
  set(OFXGRIP_OPENCL_FOUND ON)
  set(OFXGRIP_OPENCL_LIBRARIES "-framework OpenCL")
  set(OFXGRIP_OPENCL_INCLUDE_DIRS "")
else()
  find_package(OpenCL QUIET)
  set(OFXGRIP_OPENCL_FOUND ${OpenCL_FOUND})
  set(OFXGRIP_OPENCL_LIBRARIES ${OpenCL_LIBRARIES})
  set(OFXGRIP_OPENCL_INCLUDE_DIRS ${OpenCL_INCLUDE_DIRS})
endif()
if(OFXGRIP_OPENCL_FOUND)
  add_library(ofxgrip_gpu_opencl STATIC gpu/OpenCLBackend.cpp gpu/OpenCLWatermark.cpp)
  add_library(OfxGrip::gpu_opencl ALIAS ofxgrip_gpu_opencl)
  target_link_libraries(ofxgrip_gpu_opencl PUBLIC OfxGrip::core PRIVATE ${OFXGRIP_OPENCL_LIBRARIES})
  if(OFXGRIP_OPENCL_INCLUDE_DIRS)
    target_include_directories(ofxgrip_gpu_opencl SYSTEM PRIVATE ${OFXGRIP_OPENCL_INCLUDE_DIRS})
  endif()
  target_compile_definitions(ofxgrip_gpu_opencl PUBLIC OFXGRIP_HAS_OPENCL=1)
endif()
set(OFXGRIP_OPENCL_FOUND ${OFXGRIP_OPENCL_FOUND} PARENT_SCOPE)

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
  ofx/Analysis.cpp
  ofx/KernelEffect.cpp
  ofx/Interact.cpp
  ofx/RenderWatermark.cpp
  ofx/Plugin.cpp)
add_library(OfxGrip::ofxgrip ALIAS ofxgrip)
target_link_libraries(ofxgrip PUBLIC OfxGrip::core OfxGrip::openfx_headers OfxGrip::kernels)
if(APPLE)
  target_link_libraries(ofxgrip PUBLIC "-framework CoreFoundation" OfxGrip::gpu_metal)
endif()
if(TARGET ofxgrip_gpu_opencl)
  # The watermark reaches OpenCL frames through the backend; a plug-in without OpenCL frames pays
  # nothing beyond the link.
  target_link_libraries(ofxgrip PUBLIC OfxGrip::gpu_opencl)
endif()
# The OpenGL overlay fallback compiles where the OpenGL headers exist; the draw suite needs none.
if(APPLE)
  target_link_libraries(ofxgrip PRIVATE "-framework OpenGL")
  target_compile_definitions(ofxgrip PRIVATE OFXGRIP_HAS_OPENGL=1)
else()
  find_package(OpenGL QUIET)
  if(OpenGL_FOUND)
    target_link_libraries(ofxgrip PRIVATE OpenGL::GL)
    target_compile_definitions(ofxgrip PRIVATE OFXGRIP_HAS_OPENGL=1)
  endif()
endif()

# ---- ofxgrip_c: the C contract over the adapter ---------------------------------------------------

add_library(ofxgrip_c STATIC c/ofxgrip_c.cpp)
add_library(OfxGrip::c ALIAS ofxgrip_c)
target_link_libraries(ofxgrip_c PUBLIC OfxGrip::ofxgrip)
target_compile_definitions(ofxgrip_c PRIVATE OFXGRIP_VERSION_STRING="${PROJECT_VERSION}")
