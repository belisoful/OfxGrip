// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  CpuKernelBackend.hpp
	@brief The CPU backend: runs the C++ code `slangc -target cpp` emits for a kernel over the
	       host's CPU frames.

	Introduced in OfxGrip 0.4.0. The emitted C++ is compiled into the plug-in against the
	vendored Slang prelude (`external/slang-prelude`), and the kernel's entry function is
	registered through `KernelCpu.hpp`. The backend fills the globals block the C++ reflection
	JSON describes: a pointer to the packed uniforms, a pointer to the tile, one texture object
	per texture global, a sampler object, and each specialization constant as a plain value. The
	kernel writes a float tile that the backend converts into the destination's depth, so one
	build serves every depth. Textures read any depth through `readComponent`; a one-component
	frame samples as (a, 0, 0, 1), as a Metal `R` format does.
*/
#pragma once

#include <ofxgrip/gpu/KernelEngine.hpp>

OFXGRIP_NAMESPACE_BEGIN

/*! The signature of a registered CPU entry: the Slang `ComputeVaryingInput`, the entry-point
    parameters (unused), and the globals block. */
using CpuKernelEntry = void (*)(void* varyingInput, void* entryPointParams, void* globals);

/*! The backend. Always available. */
class CpuKernelBackend final : public KernelBackend {
public:
	GpuApi api() const noexcept override { return GpuApi::None; }
	bool available() const noexcept override { return true; }
	/*! Runs the kernel over the window in bands across the threading service. The destination
	    and every source must be CPU views. Fails with `InvalidParameter` when the implementation
	    has no CPU entry or its layout is not a block layout. */
	Error dispatch(const KernelDispatch& dispatch) override;
};

OFXGRIP_NAMESPACE_END
