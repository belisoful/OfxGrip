# Builds the Rust workspace under rust/ with cargo against this build's static libraries, and
# packages the Rust sample (examples/gain-rs) as an OpenFX bundle the tests load.
#
if(TARGET ofxgrip_gpu_opencl)
  set(_ofxgrip_rust_opencl 1)
else()
  set(_ofxgrip_rust_opencl 0)
endif()
if(APPLE OR OpenGL_FOUND)
  set(_ofxgrip_rust_opengl 1)
else()
  set(_ofxgrip_rust_opengl 0)
endif()

# The sys crate links the libraries from OFXGRIP_BUILD_DIR, which this file sets to the current
# build directory, so the Rust plug-in runs the same adapter the C++ plug-ins do.

find_program(OFXGRIP_CARGO cargo HINTS "$ENV{HOME}/.cargo/bin" "/opt/homebrew/opt/rustup/bin" "/usr/local/opt/rustup/bin")
if(NOT OFXGRIP_CARGO)
  message(FATAL_ERROR "OFXGRIP_BUILD_RUST is ON but cargo was not found")
endif()

# cargo found by absolute path still needs rustc on PATH; put its own directory first.
get_filename_component(_cargo_dir "${OFXGRIP_CARGO}" DIRECTORY)
if(WIN32)
  set(_cargo_env "PATH=${_cargo_dir};$ENV{PATH}")
else()
  set(_cargo_env "PATH=${_cargo_dir}:$ENV{PATH}")
endif()

set(_rust_root "${PROJECT_SOURCE_DIR}/rust")
set(_rust_target_dir "${CMAKE_BINARY_DIR}/rust-target")
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  set(_cargo_profile_flag --release)
  set(_cargo_profile_dir release)
else()
  set(_cargo_profile_flag)
  set(_cargo_profile_dir debug)
endif()

# The same architecture folder as the C++ plug-ins (see ofxgrip_bundle_arch_dir). On Windows on
# Arm the cdylib must be an ARM64EC build to match Win-arm64ec; cargo's target for that is
# arm64ec-pc-windows-msvc, passed through OFXGRIP_RUST_TARGET.
ofxgrip_bundle_arch_dir(_arch_dir)
set(OFXGRIP_RUST_TARGET "" CACHE STRING "A cargo --target triple for the Rust bundle; empty builds for the host")
set(_cargo_target_flag)
set(_cargo_out_dir "${_rust_target_dir}/${_cargo_profile_dir}")
if(OFXGRIP_RUST_TARGET)
  set(_cargo_target_flag --target "${OFXGRIP_RUST_TARGET}")
  set(_cargo_out_dir "${_rust_target_dir}/${OFXGRIP_RUST_TARGET}/${_cargo_profile_dir}")
endif()
if(APPLE)
  set(_cdylib "${_cargo_out_dir}/libOfxGripGainRs.dylib")
elseif(WIN32)
  set(_cdylib "${_cargo_out_dir}/OfxGripGainRs.dll")
else()
  set(_cdylib "${_cargo_out_dir}/libOfxGripGainRs.so")
endif()

set(OFXGRIP_RUST_BUNDLE_DIR "${CMAKE_BINARY_DIR}/plugins/GainRs/OfxGripGainRs.ofx.bundle")
set(_rust_binary "${OFXGRIP_RUST_BUNDLE_DIR}/Contents/${_arch_dir}/OfxGripGainRs.ofx")

add_custom_command(
  OUTPUT "${_cdylib}"
  COMMAND ${CMAKE_COMMAND} -E env "${_cargo_env}" "OFXGRIP_BUILD_DIR=${CMAKE_BINARY_DIR}" "CARGO_TARGET_DIR=${_rust_target_dir}" "OFXGRIP_LINK_OPENCL=${_ofxgrip_rust_opencl}" "OFXGRIP_LINK_OPENGL=${_ofxgrip_rust_opengl}"
          "${OFXGRIP_CARGO}" build ${_cargo_profile_flag} ${_cargo_target_flag} --manifest-path "${_rust_root}/Cargo.toml" --workspace
  DEPENDS ofxgrip_c ofxgrip ofxgrip_core
  WORKING_DIRECTORY "${_rust_root}"
  COMMENT "cargo build (OfxGrip Rust workspace)"
  VERBATIM)

if(APPLE)
  set(OFXGRIP_PLUGIN_NAME "OfxGripGainRs")
  set(OFXGRIP_PLUGIN_IDENTIFIER "com.belisoful.ofxgrip.gainrs")
  set(OFXGRIP_PLUGIN_VERSION "${PROJECT_VERSION}")
  set(OFXGRIP_PLUGIN_MIN_OS "${CMAKE_OSX_DEPLOYMENT_TARGET}")
  configure_file("${PROJECT_SOURCE_DIR}/cmake/Info.plist.in" "${OFXGRIP_RUST_BUNDLE_DIR}/Contents/Info.plist" @ONLY)
  set(_sign_identity "${OFXGRIP_CODESIGN_IDENTITY}")
  if(NOT _sign_identity)
    set(_sign_identity "-")
  endif()
  add_custom_command(
    OUTPUT "${_rust_binary}"
    COMMAND ${CMAKE_COMMAND} -E copy "${_cdylib}" "${_rust_binary}"
    COMMAND codesign --force --sign "${_sign_identity}" --timestamp=none "${OFXGRIP_RUST_BUNDLE_DIR}"
    DEPENDS "${_cdylib}"
    COMMENT "Packaging OfxGripGainRs.ofx.bundle"
    VERBATIM)
else()
  add_custom_command(
    OUTPUT "${_rust_binary}"
    COMMAND ${CMAKE_COMMAND} -E copy "${_cdylib}" "${_rust_binary}"
    DEPENDS "${_cdylib}"
    COMMENT "Packaging OfxGripGainRs.ofx.bundle"
    VERBATIM)
endif()

add_custom_target(ofxgrip_rust ALL DEPENDS "${_rust_binary}")
set_property(GLOBAL APPEND PROPERTY OFXGRIP_BUNDLES "${OFXGRIP_RUST_BUNDLE_DIR}")

# The Rust crates' own tests, run by ctest beside the C++ suites.
add_test(NAME rust.cargo_test
  COMMAND ${CMAKE_COMMAND} -E env "${_cargo_env}" "OFXGRIP_BUILD_DIR=${CMAKE_BINARY_DIR}" "CARGO_TARGET_DIR=${_rust_target_dir}" "OFXGRIP_LINK_OPENCL=${_ofxgrip_rust_opencl}" "OFXGRIP_LINK_OPENGL=${_ofxgrip_rust_opengl}" "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs"
          "${OFXGRIP_CARGO}" test ${_cargo_profile_flag} --manifest-path "${_rust_root}/Cargo.toml" -p ofxgrip
  WORKING_DIRECTORY "${_rust_root}")
