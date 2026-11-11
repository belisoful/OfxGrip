// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  CudaKernelBackend.hpp
	@brief The CUDA kernel backend: loads the PTX `nvcc` compiles from the CUDA source `slangc`
	       emits for a kernel, binds the kernel's globals by the reflection layout, and launches
	       it over the host's CUDA frames.

	Introduced in OfxGrip 0.5.0. The backend calls the CUDA driver API through `CudaDriver`, so
	it builds without a CUDA toolkit and is available wherever an NVIDIA driver is installed.
	It runs in the CUDA context the host has made current and on the stream the host passes.

	The binding follows the CUDA target's block layout:

	- The uniforms and the tile → device memory, by pointer.
	- A source → a texture object with normalized coordinates, bilinear filtering, and
	  clamp-to-edge addressing. The object sits over the host's pitched memory when the frame
	  meets the device's texture alignment, and over an array filled by a copy otherwise.
	- The destination → a surface object over a float RGBA array, copied into the window of the
	  host's frame after the launch.
	- A specialization constant → a plain value in the block.

	The emitted kernel writes raw float RGBA, so the destination frame must be 32-bit float RGBA.
	The globals block lives in the module's constant memory, which every launch of that module
	shares, so the backend waits for the stream before it returns.

	The PTX is the product of an NVIDIA toolchain: `scripts/ofxgrip-slang.sh --targets cuda
	--nvcc <path>` emits it. A kernel without a PTX build has no CUDA implementation, and its
	effect does not declare CUDA to the host.
*/
#pragma once

#include <ofxgrip/gpu/CudaDriver.hpp>
#include <ofxgrip/gpu/KernelEngine.hpp>

#include <memory>

OFXGRIP_NAMESPACE_BEGIN

/*! The name of the constant global the emitted CUDA source declares for the kernel's globals. */
inline constexpr const char* kCudaGlobalsName = "SLANG_globalParams";

/*! The backend. */
class CudaKernelBackend final : public KernelBackend {
public:
	/*! Over the machine's driver. */
	CudaKernelBackend();
	/*! Over a driver table that outlives the backend. A null table makes the backend
	    unavailable. */
	explicit CudaKernelBackend(const cuda::CudaDriver* driver);
	~CudaKernelBackend() override;

	GpuApi api() const noexcept override { return GpuApi::Cuda; }
	/*! Whether a complete driver is loaded. A current context is checked at dispatch. */
	bool available() const noexcept override;
	Error dispatch(const KernelDispatch& dispatch) override;
	/*! The number of loaded modules the backend holds. */
	std::size_t cachedModuleCount() const;
	/*! Registers the backend with `KernelEngine::shared()`. Idempotent. */
	static void registerWithEngine();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

OFXGRIP_NAMESPACE_END
