// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  KernelEngine.hpp
	@brief The engine that dispatches a compiled kernel on the backend a frame lives in, and the
	       backend interface the Metal, CPU, and further backends implement.

	Introduced in OfxGrip 0.4.0. A `KernelDispatch` names the kernel's layout and binding plan,
	the packed uniforms and constants, the destination window, and the images the kernel reads,
	each as a host GPU frame or a CPU view. The engine picks the backend by the destination's
	API. Every backend fills the same `GripTile`, binds the same roles, and writes the same
	pixels within the tolerance the conformance tests state, so an effect renders alike on every
	path. Host libraries plan, section 10.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/HostServices.hpp>
#include <ofxgrip/core/Image.hpp>
#include <ofxgrip/core/KernelCatalog.hpp>
#include <ofxgrip/core/KernelLayout.hpp>
#include <ofxgrip/core/KernelValues.hpp>
#include <ofxgrip/core/Types.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! One image a dispatch binds: a host GPU frame or a CPU view. A disconnected image binds
    transparent black. */
struct KernelImage {
	bool connected{false};
	GpuImage gpu;
	ImageView cpu;
	bool isGpu() const noexcept { return gpu.api != GpuApi::None; }
	/*! The bounds of whichever form is set. */
	RectI bounds() const noexcept { return isGpu() ? gpu.desc.bounds : cpu.desc.bounds; }
};

/*! One dispatch. The pointers stay valid for the call. */
struct KernelDispatch {
	const KernelLayout* layout{nullptr};
	const KernelImplementation* implementation{nullptr};
	const KernelBindingPlan* plan{nullptr};
	std::string cacheKey;                        /*!< Names the compiled program in a backend's cache. */
	Bytes uniforms;
	std::vector<KernelConstantValue> constants;
	RectI window;                                /*!< The destination pixels to write. */
	Point2D pixelScale{1, 1};
	KernelImage destination;
	std::vector<KernelImage> sources;            /*!< The positional source images, in order. */
	std::map<ParameterId, KernelImage> keyedImages; /*!< The images of keyed textures, by parameter. */
	ThreadingService* threading{nullptr};        /*!< For the CPU backend; null runs single-threaded. */
	std::function<bool()> abort;                 /*!< Polled between bands on the CPU backend. */
};

/*! A backend. */
class KernelBackend {
public:
	virtual ~KernelBackend() = default;
	virtual GpuApi api() const noexcept = 0;
	/*! Whether the backend can run in this process. */
	virtual bool available() const noexcept = 0;
	/*! Runs a dispatch. A GPU backend encodes on the host's queue and returns without waiting. */
	virtual Error dispatch(const KernelDispatch& dispatch) = 0;
};

/*! The engine. One per process; backends register once. */
class KernelEngine {
public:
	static KernelEngine& shared();
	/*! Registers a backend for its API, replacing an earlier one. */
	void registerBackend(std::unique_ptr<KernelBackend> backend);
	KernelBackend* backend(GpuApi api) const noexcept;
	/*! The APIs with a registered, available backend. */
	std::vector<GpuApi> availableApis() const;
	/*! Runs a dispatch on the destination's backend. Fails with `MissingHostFeature` when no
	    backend serves that API, and `InvalidParameter` when the dispatch lacks a layout, an
	    implementation, or a plan. */
	Error run(const KernelDispatch& dispatch);

private:
	KernelEngine();
	std::map<GpuApi, std::unique_ptr<KernelBackend>> backends_;
};

/*! The image a plan's texture binds: the keyed image, the positional source, or none. */
const KernelImage* kernelImageFor(const KernelDispatch& dispatch, const KernelBindingPlan::Texture& texture) noexcept;

OFXGRIP_NAMESPACE_END
