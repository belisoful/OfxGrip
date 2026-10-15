// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  KernelCpu.hpp
	@brief The glue between a kernel's emitted C++ and the CPU backend: the entry adapter the
	       build generates, and the record a plug-in registers with its catalog.

	Introduced in OfxGrip 0.4.0. `ofxgrip_add_kernels` (cmake/OfxGripKernels.cmake) compiles each
	kernel's emitted C++ into the plug-in through a generated adapter that defines
	`ofxgrip_cpu_kernel_<name>` with the `CpuKernelEntry` signature, and embeds the kernel's
	sources and reflection JSON as byte arrays. `OFXGRIP_KERNEL(name)` declares those symbols and
	yields the `KernelRecord` with every target the build emitted.
*/
#pragma once

#include <ofxgrip/core/KernelCatalog.hpp>

#include <cstddef>
#include <string>

OFXGRIP_NAMESPACE_BEGIN

/*! One embedded kernel, as the generated adapter describes it. */
struct EmbeddedKernel {
	const char* name;
	const char* entryPoint;
	const unsigned char* slangSource;
	std::size_t slangLength;
	const unsigned char* metalSource;
	std::size_t metalLength;
	const unsigned char* metalLayout;
	std::size_t metalLayoutLength;
	const unsigned char* cppLayout;
	std::size_t cppLayoutLength;
	void* cpuEntry;
};

/*! The catalog record of an embedded kernel: the universal Metal build when the Metal source
    was emitted, and the CPU build when the entry exists. */
KernelRecord kernelRecordFor(const EmbeddedKernel& kernel);

OFXGRIP_NAMESPACE_END

/*! Declares the generated adapter's record of a kernel. Place it at global scope, once per
    translation unit that names the kernel. */
#define OFXGRIP_DECLARE_KERNEL(name) extern const ::ofxgrip::EmbeddedKernel ofxgrip_embedded_kernel_##name
/*! The `EmbeddedKernel` of a declared kernel. */
#define OFXGRIP_KERNEL(name) (::ofxgrip_embedded_kernel_##name)
