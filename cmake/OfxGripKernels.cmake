# ofxgrip_add_kernels: compiles Slang kernels into a target and embeds them for the kernel engine.
#
#   ofxgrip_add_kernels(TARGET <target> KERNELS <kernel>.slang ... [ENTRY <name>])
#
# For each <kernel>.slang the function expects, beside it, the files scripts/ofxgrip-slang.sh
# emits: <kernel>.metal, <kernel>.metal.json, <kernel>.cpp, and <kernel>.cpp.json. They are checked
# in, so a build without slangc uses them. When OFXGRIP_SLANGC names a slangc executable, a custom
# command regenerates them whenever the Slang source or the GripKernel module is newer.
#
# The function generates one adapter translation unit per kernel (cmake/KernelAdapter.cpp.in) that
# compiles the emitted C++ against the vendored Slang prelude, defines the CPU entry, and embeds
# the four files as byte arrays. The plug-in names the kernel with OFXGRIP_KERNEL(<kernel>) and
# registers kernelRecordFor(...) with its catalog. The kernel files are also copied into the
# bundle's Contents/Resources/Kernels/ when the target is a plug-in built by ofxgrip_add_plugin.

set(OFXGRIP_SLANGC "" CACHE FILEPATH "The slangc executable; empty uses the checked-in kernel outputs")

function(_ofxgrip_file_bytes path out_var)
  if(EXISTS "${path}")
    file(READ "${path}" _hex HEX)
    string(LENGTH "${_hex}" _len)
    if(_len GREATER 0)
      string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
      set(${out_var} "${_bytes}0" PARENT_SCOPE)
      return()
    endif()
  endif()
  set(${out_var} "0" PARENT_SCOPE)
endfunction()

function(ofxgrip_add_kernels)
  set(options)
  set(oneValueArgs TARGET ENTRY)
  set(multiValueArgs KERNELS)
  cmake_parse_arguments(OGK "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  if(NOT OGK_TARGET OR NOT OGK_KERNELS)
    message(FATAL_ERROR "ofxgrip_add_kernels needs TARGET and KERNELS")
  endif()
  if(NOT OGK_ENTRY)
    set(OGK_ENTRY "fxGripKernel")
  endif()
  set(_module_dir "${PROJECT_SOURCE_DIR}/kernels")
  get_target_property(_bundle_dir ${OGK_TARGET} OFXGRIP_BUNDLE_DIR)

  foreach(_kernel IN LISTS OGK_KERNELS)
    get_filename_component(_kernel_abs "${_kernel}" ABSOLUTE)
    get_filename_component(_kernel_dir "${_kernel_abs}" DIRECTORY)
    get_filename_component(_name "${_kernel_abs}" NAME_WE)
    set(_metal "${_kernel_dir}/${_name}.metal")
    set(_metal_json "${_kernel_dir}/${_name}.metal.json")
    set(_cpp "${_kernel_dir}/${_name}.cpp")
    set(_cpp_json "${_kernel_dir}/${_name}.cpp.json")

    if(OFXGRIP_SLANGC)
      add_custom_command(
        OUTPUT "${_metal}" "${_metal_json}" "${_cpp}" "${_cpp_json}"
        COMMAND "${CMAKE_COMMAND}" -E env "OFXGRIP_SLANGC=${OFXGRIP_SLANGC}" "OFXGRIP_KERNEL_MODULE_DIR=${_module_dir}"
                bash "${PROJECT_SOURCE_DIR}/scripts/ofxgrip-slang.sh" --entry "${OGK_ENTRY}" --targets metal,cpp "${_kernel_abs}"
        DEPENDS "${_kernel_abs}" "${_module_dir}/GripKernel.slang" "${PROJECT_SOURCE_DIR}/scripts/ofxgrip-slang.sh"
        COMMENT "slangc ${_name}.slang"
        VERBATIM)
    endif()
    foreach(_needed IN ITEMS "${_cpp}" "${_cpp_json}" "${_metal_json}")
      if(NOT OFXGRIP_SLANGC AND NOT EXISTS "${_needed}")
        message(FATAL_ERROR "ofxgrip_add_kernels: ${_needed} is missing; run scripts/ofxgrip-slang.sh ${_kernel} or set OFXGRIP_SLANGC")
      endif()
    endforeach()

    # The adapter embeds the files at configure time; a regenerated file reconfigures.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_kernel_abs}" "${_metal}" "${_metal_json}" "${_cpp_json}")
    set(OFXGRIP_KERNEL_NAME "${_name}")
    set(OFXGRIP_KERNEL_ENTRY "${OGK_ENTRY}")
    set(OFXGRIP_KERNEL_CPP "${_cpp}")
    _ofxgrip_file_bytes("${_kernel_abs}" OFXGRIP_KERNEL_SLANG_BYTES)
    _ofxgrip_file_bytes("${_metal}" OFXGRIP_KERNEL_METAL_BYTES)
    _ofxgrip_file_bytes("${_metal_json}" OFXGRIP_KERNEL_METAL_JSON_BYTES)
    _ofxgrip_file_bytes("${_cpp_json}" OFXGRIP_KERNEL_CPP_JSON_BYTES)
    set(_adapter "${CMAKE_CURRENT_BINARY_DIR}/${OGK_TARGET}_kernel_${_name}.cpp")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/KernelAdapter.cpp.in" "${_adapter}" @ONLY)
    target_sources(${OGK_TARGET} PRIVATE "${_adapter}")
    set_source_files_properties("${_adapter}" PROPERTIES OBJECT_DEPENDS "${_cpp}")
    # The emitted C++ is generated code: it compiles without the project's warning set.
    if(MSVC)
      set_source_files_properties("${_adapter}" PROPERTIES COMPILE_OPTIONS "/w")
    else()
      set_source_files_properties("${_adapter}" PROPERTIES COMPILE_OPTIONS "-w")
    endif()
    target_link_libraries(${OGK_TARGET} PRIVATE OfxGrip::kernels)

    if(_bundle_dir)
      set(_resources "${_bundle_dir}/Contents/Resources/Kernels")
      add_custom_command(TARGET ${OGK_TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_resources}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_kernel_abs}" "${_metal}" "${_metal_json}" "${_cpp_json}" "${_resources}/"
        COMMENT "Bundling kernel ${_name}"
        VERBATIM)
    endif()
  endforeach()
endfunction()
