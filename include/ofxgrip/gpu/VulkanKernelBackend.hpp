// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  VulkanKernelBackend.hpp
	@brief The Vulkan kernel backend: runs the SPIR-V `slangc` emits for a kernel on OfxGrip's own
	       Vulkan device, over the host's CPU frames.

	Introduced in OfxGrip 0.5.0. No OpenFX host delivers Vulkan frames, so this backend is an
	accelerator for CPU renders: a dispatch names it in `KernelDispatch::accelerator`. The
	backend converts each source view to float RGBA, uploads it, binds the kernel's globals by
	the SPIR-V reflection layout (one descriptor index space in set 0, constants by
	specialization ID), dispatches, reads the float destination back, and converts it into the
	destination's depth. The conversions are the CPU backend's, so one SPIR-V build serves every
	depth and layout. The backend waits for the device before it returns.

	The whole source view is uploaded for each dispatch, so a host that renders in small tiles
	pays the upload per tile.
*/
#pragma once

#include <ofxgrip/gpu/KernelEngine.hpp>

OFXGRIP_NAMESPACE_BEGIN

/*! The backend. */
class VulkanKernelBackend final : public KernelBackend {
public:
	GpuApi api() const noexcept override { return GpuApi::Vulkan; }
	/*! Whether the process can open a Vulkan device. The first call opens it. */
	bool available() const noexcept override;
	/*! Runs the kernel over the window. The destination and every source must be CPU views, and
	    the layout must be the SPIR-V target's descriptor layout. */
	Error dispatch(const KernelDispatch& dispatch) override;
	/*! Registers the backend with `KernelEngine::shared()`. Idempotent. Opens no device. */
	static void registerWithEngine();
};

OFXGRIP_NAMESPACE_END
