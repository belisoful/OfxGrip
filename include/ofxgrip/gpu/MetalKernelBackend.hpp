// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  MetalKernelBackend.hpp
	@brief The Metal kernel backend: compiles the Metal source `slangc` emits, binds the kernel's
	       globals by the reflection layout, and dispatches a tile over the host's `MTLBuffer`
	       frames.

	Introduced in OfxGrip 0.4.0. macOS only. A host frame becomes a texture over its buffer when
	the rows are aligned for one, and an intermediate texture filled by a blit otherwise; the
	destination renders in place when the window covers the frame and the rows allow it, and
	through an intermediate texture that a blit copies into the window otherwise. On the Fusion
	page the source is copied to a scratch buffer first (Q-RESOLVE-031). The backend encodes on
	the host's queue and commits without waiting.
*/
#pragma once

#include <ofxgrip/gpu/KernelEngine.hpp>

OFXGRIP_NAMESPACE_BEGIN

/*! The backend. Registers itself with the engine at static initialization when the Metal
    backend library is linked. */
class MetalKernelBackend final : public KernelBackend {
public:
	GpuApi api() const noexcept override { return GpuApi::Metal; }
	bool available() const noexcept override;
	Error dispatch(const KernelDispatch& dispatch) override;
	/*! Registers the backend with `KernelEngine::shared()`. Idempotent. */
	static void registerWithEngine();
};

OFXGRIP_NAMESPACE_END
