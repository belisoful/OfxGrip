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

	A driver loads PTX from a toolkit of its own release or an older one, and refuses PTX
	from a newer toolkit (`CUDA_ERROR_UNSUPPORTED_PTX_VERSION`). `accepts` compares the two
	releases, so an effect whose PTX is newer than the installed driver declares no CUDA
	render and the host hands it another kind of frame.
*/
#pragma once

#include <ofxgrip/gpu/CudaDriver.hpp>
#include <ofxgrip/gpu/KernelEngine.hpp>

#include <memory>
#include <string_view>

OFXGRIP_NAMESPACE_BEGIN

/*! The name of the constant global the emitted CUDA source declares for the kernel's globals. */
inline constexpr const char* kCudaGlobalsName = "SLANG_globalParams";

/*! The CUDA release of the toolkit that compiled a PTX module, as 1000 × major + 10 × minor
    (13050 for 13.5). `nvcc` states it in a comment at the top of the module:
    `// Cuda compilation tools, release 13.5, V13.5.37`. 0 when the text has no such line.
    Introduced in OfxGrip 0.5.0. */
int cudaToolkitVersionOf(std::string_view ptx) noexcept;

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
	/*! Whether the driver's release is at least the release of the toolkit that built the
	    module. A module or a driver that states no release is accepted, and the load decides.
	    An unavailable backend accepts nothing. */
	bool accepts(const KernelImplementation& implementation) const noexcept override;
	Error dispatch(const KernelDispatch& dispatch) override;
	/*! Device memory in the current context. Its owner frees the memory, so the owner is
	    released on the render thread while that context is current.
	    Introduced in OfxGrip 0.5.0. */
	Error makeScratch(const KernelImage& like, const RectI& bounds, KernelImage& out) override;
	/*! The number of loaded modules the backend holds. */
	std::size_t cachedModuleCount() const;
	/*! Unloads the modules of the current context and forgets every module. A module of
	    another context goes with that context. */
	void releaseDeviceState() override;
	/*! Registers the backend with `KernelEngine::shared()`. Idempotent. */
	static void registerWithEngine();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

OFXGRIP_NAMESPACE_END
