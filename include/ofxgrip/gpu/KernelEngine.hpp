// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  KernelEngine.hpp
	@brief The engine that dispatches a compiled kernel on the backend a frame lives in, and the
	       backend interface the Metal, CPU, and further backends implement.

	Introduced in OfxGrip 0.4.0. A `KernelDispatch` names the kernel's layout and binding plan,
	the packed uniforms and constants, the destination window, and the images the kernel reads,
	each as a host GPU frame or a CPU view. The engine picks the backend by the destination's
	API, and by the dispatch's accelerator for a CPU destination. Every backend fills the same `GripTile`, binds the same roles, and writes the same
	pixels within the tolerance the conformance tests state, so an effect renders alike on every
	path. Host libraries plan, section 10.

	A dispatch can carry a colourspace conversion (`KernelColorPass`). The engine then converts
	each color source into a scratch image, runs the kernel into a scratch destination, and
	converts the result into the real destination. The conversion is the embedded
	`GripColorConvert` kernel (`KernelColor.hpp`), so it runs on the frame's own backend.
*/
#pragma once

#include <ofxgrip/core/ColorGamut.hpp>
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
	/*! What keeps a scratch image's pixels alive. Null for a host frame. A GPU backend holds
	    the owner of every image it binds until its encoded work completes. Introduced in
	    OfxGrip 0.5.0. */
	std::shared_ptr<void> owner;
	bool isGpu() const noexcept { return gpu.api != GpuApi::None; }
	/*! The bounds of whichever form is set. */
	RectI bounds() const noexcept { return isGpu() ? gpu.desc.bounds : cpu.desc.bounds; }
	/*! The layout of whichever form is set. Introduced in OfxGrip 0.5.0. */
	const ImageDesc& desc() const noexcept { return isGpu() ? gpu.desc : cpu.desc; }
};

/*! The colourspace conversion around a dispatch. Introduced in OfxGrip 0.5.0.

    - `toKernel` → applied to every connected RGB or RGBA source before the kernel reads it.
    - `toHost` → applied to the kernel's result before it reaches the destination.

    An alpha-only source is not a color image and binds unconverted. */
struct KernelColorPass {
	ColorConversion toKernel;
	ColorConversion toHost;
	bool active() const noexcept { return !toKernel.isIdentity() || !toHost.isIdentity(); }
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
	/*! The backend that runs a dispatch whose destination is a CPU view. `None` is the CPU
	    backend; `Vulkan` uploads the frames to OfxGrip's own device and reads the result back.
	    Ignored for a GPU destination, which always runs on its own API. Introduced in OfxGrip
	    0.5.0. */
	GpuApi accelerator{GpuApi::None};
	std::vector<KernelImage> sources;            /*!< The positional source images, in order. */
	std::map<ParameterId, KernelImage> keyedImages; /*!< The images of keyed textures, by parameter. */
	ThreadingService* threading{nullptr};        /*!< For the CPU backend; null runs single-threaded. */
	std::function<bool()> abort;                 /*!< Polled between bands on the CPU backend. */
	KernelColorPass color;                       /*!< The conversion around the kernel. Introduced in OfxGrip 0.5.0. */
};

/*! A backend. */
class KernelBackend {
public:
	virtual ~KernelBackend() = default;
	virtual GpuApi api() const noexcept = 0;
	/*! Whether the backend can run in this process. */
	virtual bool available() const noexcept = 0;
	/*! Whether this machine can load a build made for the backend. A kernel effect declares a
	    GPU path to the host only when its backend accepts every kernel's build. The default
	    accepts every build.
	    Introduced in OfxGrip 0.5.0. */
	virtual bool accepts(const KernelImplementation& implementation) const noexcept
	{
		static_cast<void>(implementation);
		return true;
	}
	/*! Runs a dispatch. A GPU backend encodes on the host's queue and returns without waiting.
	    A backend ignores the dispatch's `color`: the engine applies it. */
	virtual Error dispatch(const KernelDispatch& dispatch) = 0;
	/*! Makes a 32-bit float RGBA image over `bounds` that this backend reads and writes, on the
	    device and queue of `like`. The engine uses it for the intermediates of a colourspace
	    conversion. The default makes none and fails with `Unsupported`.
	    Introduced in OfxGrip 0.5.0. */
	virtual Error makeScratch(const KernelImage& like, const RectI& bounds, KernelImage& out);
	/*! Drops what the backend holds on a device or a driver: compiled programs, modules, a
	    device of its own. A later dispatch builds them again. Called when no dispatch is in
	    flight. The default holds nothing.
	    Introduced in OfxGrip 0.5.0. */
	virtual void releaseDeviceState() {}
};

/*! The engine. One per process; backends register once.

    The shared engine is never destroyed. A plug-in's static destructors run while the
    operating system unloads the binary (under the loader lock on Windows), and a GPU driver
    called there can wait forever. The adapter calls `releaseDeviceState` from the unload action
    instead, which runs before the binary is unloaded.

    The backend table has no lock. Backends register from the load action and are released from
    the unload action, and neither runs beside a render. */
class KernelEngine {
public:
	static KernelEngine& shared();
	/*! Has every backend drop its device state. Introduced in OfxGrip 0.5.0. */
	void releaseDeviceState();
	/*! Registers a backend for its API, replacing an earlier one. */
	void registerBackend(std::unique_ptr<KernelBackend> backend);
	KernelBackend* backend(GpuApi api) const noexcept;
	/*! The APIs with a registered, available backend. */
	std::vector<GpuApi> availableApis() const;
	/*! Runs a dispatch on the destination's backend, or on the dispatch's accelerator when the
	    destination is a CPU view. Fails with `MissingHostFeature` when no backend serves that
	    API, and `InvalidParameter` when the dispatch lacks a layout, an implementation, or a
	    plan. A dispatch with an active `color` fails with `Unsupported` where
	    `convertsColorOn` is false. */
	Error run(const KernelDispatch& dispatch);
	/*! Whether a colourspace conversion runs on frames of an API on this machine: the backend
	    is available, the conversion kernel has a build for it, and the backend accepts that
	    build. `None` is the CPU path. Introduced in OfxGrip 0.5.0. */
	bool convertsColorOn(GpuApi api) const noexcept;

private:
	KernelEngine();
	Error runConverted(KernelBackend& target, const KernelDispatch& dispatch);
	std::map<GpuApi, std::unique_ptr<KernelBackend>> backends_;
};

/*! The image a plan's texture binds: the keyed image, the positional source, or none. */
const KernelImage* kernelImageFor(const KernelDispatch& dispatch, const KernelBindingPlan::Texture& texture) noexcept;

OFXGRIP_NAMESPACE_END
