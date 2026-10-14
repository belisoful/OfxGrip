# ofxgrip_add_plugin: builds an OpenFX bundle from sources that link OfxGrip.
#
#   ofxgrip_add_plugin(NAME Gain
#                      IDENTIFIER com.belisoful.ofxgrip.gain
#                      SOURCES Gain.cpp
#                      [LIBRARIES ...])
#
# The target is a MODULE library named <NAME>.ofx placed in the OpenFX bundle layout:
#   <build>/plugins/<NAME>/<NAME>.ofx.bundle/Contents/<Arch>/<NAME>.ofx
#   <build>/plugins/<NAME>/<NAME>.ofx.bundle/Contents/Info.plist   (macOS)
# <Arch> is MacOS, Win64, Linux-x86-64, or Linux-aarch64 (ofxPackaging.rst). Resolve on Windows
# ARM64 also scans Win-arm64x and Win-arm64ec; those are produced by a native ARM64 build.

function(ofxgrip_add_plugin)
  set(options)
  set(oneValueArgs NAME IDENTIFIER)
  set(multiValueArgs SOURCES LIBRARIES)
  cmake_parse_arguments(OGP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  if(NOT OGP_NAME OR NOT OGP_IDENTIFIER)
    message(FATAL_ERROR "ofxgrip_add_plugin needs NAME and IDENTIFIER")
  endif()

  if(APPLE)
    set(_arch_dir "MacOS")
  elseif(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
      set(_arch_dir "Win-arm64")
    else()
      set(_arch_dir "Win64")
    endif()
  else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
      set(_arch_dir "Linux-aarch64")
    else()
      set(_arch_dir "Linux-x86-64")
    endif()
  endif()

  set(_bundle_dir "${CMAKE_CURRENT_BINARY_DIR}/${OGP_NAME}.ofx.bundle")
  set(_target "ofxgrip_plugin_${OGP_NAME}")

  add_library(${_target} MODULE ${OGP_SOURCES})
  target_link_libraries(${_target} PRIVATE OfxGrip::ofxgrip ${OGP_LIBRARIES})
  set_target_properties(${_target} PROPERTIES
    OUTPUT_NAME "${OGP_NAME}"
    PREFIX ""
    SUFFIX ".ofx"
    LIBRARY_OUTPUT_DIRECTORY "${_bundle_dir}/Contents/${_arch_dir}"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${_bundle_dir}/Contents/${_arch_dir}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${_bundle_dir}/Contents/${_arch_dir}"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_bundle_dir}/Contents/${_arch_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${_bundle_dir}/Contents/${_arch_dir}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_bundle_dir}/Contents/${_arch_dir}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_bundle_dir}/Contents/${_arch_dir}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_bundle_dir}/Contents/${_arch_dir}"
    OFXGRIP_BUNDLE_DIR "${_bundle_dir}")
  if(NOT WIN32)
    # The host resolves OfxGetPlugin and OfxGetNumberOfPlugins by name; keep them exported.
    set_target_properties(${_target} PROPERTIES CXX_VISIBILITY_PRESET hidden)
  endif()

  if(APPLE)
    set(OFXGRIP_PLUGIN_NAME "${OGP_NAME}")
    set(OFXGRIP_PLUGIN_IDENTIFIER "${OGP_IDENTIFIER}")
    set(OFXGRIP_PLUGIN_VERSION "${PROJECT_VERSION}")
    set(OFXGRIP_PLUGIN_MIN_OS "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/Info.plist.in" "${_bundle_dir}/Contents/Info.plist" @ONLY)
  endif()

  if(APPLE)
    # Sign the whole bundle after linking. Ad-hoc by default; OFXGRIP_CODESIGN_IDENTITY selects a
    # Developer ID for distribution. The linker's own signature covers only the binary, and a
    # bundle whose binary is signed without its resources fails `codesign --verify`.
    set(_identity "${OFXGRIP_CODESIGN_IDENTITY}")
    if(NOT _identity)
      set(_identity "-")
    endif()
    add_custom_command(TARGET ${_target} POST_BUILD
      COMMAND codesign --force --sign "${_identity}" --timestamp=none "${_bundle_dir}"
      COMMENT "Signing ${OGP_NAME}.ofx.bundle"
      VERBATIM)
  endif()

  add_custom_target(${_target}_bundle ALL DEPENDS ${_target})
  set_property(GLOBAL APPEND PROPERTY OFXGRIP_BUNDLES "${_bundle_dir}")
endfunction()
