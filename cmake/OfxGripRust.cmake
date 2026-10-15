# Builds the Rust workspace under rust/ with cargo against this build's static libraries, and
# packages the Rust sample (examples/gain-rs) as an OpenFX bundle the tests load.
#
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

if(APPLE)
  set(_cdylib "${_rust_target_dir}/${_cargo_profile_dir}/libOfxGripGainRs.dylib")
  set(_arch_dir "MacOS")
elseif(WIN32)
  set(_cdylib "${_rust_target_dir}/${_cargo_profile_dir}/OfxGripGainRs.dll")
  set(_arch_dir "Win64")
else()
  set(_cdylib "${_rust_target_dir}/${_cargo_profile_dir}/libOfxGripGainRs.so")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_arch_dir "Linux-aarch64")
  else()
    set(_arch_dir "Linux-x86-64")
  endif()
endif()

set(OFXGRIP_RUST_BUNDLE_DIR "${CMAKE_BINARY_DIR}/plugins/GainRs/OfxGripGainRs.ofx.bundle")
set(_rust_binary "${OFXGRIP_RUST_BUNDLE_DIR}/Contents/${_arch_dir}/OfxGripGainRs.ofx")

add_custom_command(
  OUTPUT "${_cdylib}"
  COMMAND ${CMAKE_COMMAND} -E env "${_cargo_env}" "OFXGRIP_BUILD_DIR=${CMAKE_BINARY_DIR}" "CARGO_TARGET_DIR=${_rust_target_dir}"
          "${OFXGRIP_CARGO}" build ${_cargo_profile_flag} --manifest-path "${_rust_root}/Cargo.toml" --workspace
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
  COMMAND ${CMAKE_COMMAND} -E env "${_cargo_env}" "OFXGRIP_BUILD_DIR=${CMAKE_BINARY_DIR}" "CARGO_TARGET_DIR=${_rust_target_dir}" "OFXGRIP_LOG_DIR=${CMAKE_BINARY_DIR}/test-logs"
          "${OFXGRIP_CARGO}" test ${_cargo_profile_flag} --manifest-path "${_rust_root}/Cargo.toml" -p ofxgrip
  WORKING_DIRECTORY "${_rust_root}")
