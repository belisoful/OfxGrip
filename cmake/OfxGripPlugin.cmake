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
# <Arch> comes from ofxgrip_bundle_arch_dir below.

# ofxgrip_bundle_arch_dir(<out-var>): the architecture folder of the OpenFX bundle layout for
# the current target (ofxPackaging.rst): MacOS (one universal binary), Win64, Linux-x86-64, or
# Linux-aarch64. On Windows on Arm, Resolve's host is an ARM64EC process that scans
# Win-arm64x, then Win-arm64ec, then Win64; it never scans Win-arm64, and it cannot load a plain
# ARM64 DLL. An ARM64EC build (`cmake -A ARM64EC`) therefore lands in Win-arm64ec; a plain ARM64
# build lands in Win-arm64 with a warning. OFXGRIP_WINDOWS_ARCH_DIR overrides the Windows choice,
# for example Win-arm64x for a binary linked as ARM64X.
set(OFXGRIP_WINDOWS_ARCH_DIR "" CACHE STRING "The Windows architecture folder of built bundles; empty derives it from the target")
option(OFXGRIP_STATIC_CXX_RUNTIME "On Linux, link libstdc++ and libgcc into each plug-in, so the bundle loads on a distribution older than the build machine's compiler" ON)
set(OFXGRIP_AUTHENTICODE_COMMAND "" CACHE STRING "On Windows, a command that signs a binary, run with the built .ofx as its last argument (for example: signtool sign /fd SHA256 /a /tr <url> /td SHA256); empty leaves it unsigned")

function(ofxgrip_bundle_arch_dir out_var)
  if(APPLE)
    set(${out_var} "MacOS" PARENT_SCOPE)
    return()
  endif()
  if(WIN32)
    if(OFXGRIP_WINDOWS_ARCH_DIR)
      set(${out_var} "${OFXGRIP_WINDOWS_ARCH_DIR}" PARENT_SCOPE)
      return()
    endif()
    if(CMAKE_GENERATOR_PLATFORM MATCHES "^(ARM64EC|arm64ec)$" OR CMAKE_CXX_FLAGS MATCHES "/arm64EC" OR CMAKE_C_FLAGS MATCHES "/arm64EC")
      set(${out_var} "Win-arm64ec" PARENT_SCOPE)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64|arm64")
      message(WARNING "A plain ARM64 Windows build lands in Win-arm64, which DaVinci Resolve does not scan; build with `cmake -A ARM64EC` for Win-arm64ec, or set OFXGRIP_WINDOWS_ARCH_DIR")
      set(${out_var} "Win-arm64" PARENT_SCOPE)
    else()
      set(${out_var} "Win64" PARENT_SCOPE)
    endif()
    return()
  endif()
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(${out_var} "Linux-aarch64" PARENT_SCOPE)
  else()
    set(${out_var} "Linux-x86-64" PARENT_SCOPE)
  endif()
endfunction()

function(ofxgrip_add_plugin)
  set(options)
  set(oneValueArgs NAME IDENTIFIER)
  set(multiValueArgs SOURCES LIBRARIES)
  cmake_parse_arguments(OGP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  if(NOT OGP_NAME OR NOT OGP_IDENTIFIER)
    message(FATAL_ERROR "ofxgrip_add_plugin needs NAME and IDENTIFIER")
  endif()

  ofxgrip_bundle_arch_dir(_arch_dir)

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
    # The host resolves OfxGetPlugin and OfxGetNumberOfPlugins by name; keep them exported and
    # nothing else, so two plug-ins built on different OfxGrip or kernel versions never clash in
    # one host process.
    set_target_properties(${_target} PROPERTIES CXX_VISIBILITY_PRESET hidden)
    if(APPLE)
      target_link_options(${_target} PRIVATE "-Wl,-exported_symbols_list,${PROJECT_SOURCE_DIR}/cmake/ofx-exports.txt")
    else()
      target_link_options(${_target} PRIVATE "-Wl,--version-script=${PROJECT_SOURCE_DIR}/cmake/ofx-exports.map")
    endif()
  endif()

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OFXGRIP_STATIC_CXX_RUNTIME AND NOT OFXGRIP_SANITIZER)
    # A host ships its own libstdc++, often older than the build machine's. The plug-in exports
    # two C functions, so its C++ runtime stays private. The C library still sets the oldest
    # distribution a bundle loads on: build on the oldest one to support.
    target_link_options(${_target} PRIVATE -static-libstdc++ -static-libgcc)
  endif()

  if(WIN32 AND OFXGRIP_AUTHENTICODE_COMMAND)
    separate_arguments(_ofxgrip_sign_command NATIVE_COMMAND "${OFXGRIP_AUTHENTICODE_COMMAND}")
    add_custom_command(TARGET ${_target} POST_BUILD
      COMMAND ${_ofxgrip_sign_command} "$<TARGET_FILE:${_target}>"
      COMMENT "Signing ${OGP_NAME}.ofx"
      VERBATIM)
  endif()

  if(APPLE)
    set(OFXGRIP_PLUGIN_NAME "${OGP_NAME}")
    set(OFXGRIP_PLUGIN_IDENTIFIER "${OGP_IDENTIFIER}")
    set(OFXGRIP_PLUGIN_VERSION "${PROJECT_VERSION}")
    set(OFXGRIP_PLUGIN_MIN_OS "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/Info.plist.in" "${_bundle_dir}/Contents/Info.plist" @ONLY)
    # The signature seals the Info.plist. A configure that rewrites it (a version change) must
    # relink, so the POST_BUILD step below signs the bundle again.
    set_property(TARGET ${_target} APPEND PROPERTY LINK_DEPENDS "${_bundle_dir}/Contents/Info.plist")
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
