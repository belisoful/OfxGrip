// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Inference.hpp
	@brief The seam between an effect and the engine that runs a machine-learning model: the
	       request and result, the backend interface, a passthrough backend, a per-frame result
	       cache, and the runner that keeps the engine off the render thread.

	Introduced in OfxGrip 0.5.0. This is FxGrip's `FxGripInferenceBackend` seam. A backend turns
	named inputs and scalar parameters into named outputs, synchronously. The framework links no
	engine. An adapter implements `InferenceBackend` over InferKit's C interface
	(`inference/InferKitBackend.hpp`), a remote service, or an effect's own runtime.

	Inference takes seconds and a render call may not. `InferenceRunner` runs the backend on its
	own thread while the render thread waits and polls for an abort, and `InferenceCache` keeps
	each frame's output so a model runs once per frame and signature.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Image.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

OFXGRIP_NAMESPACE_BEGIN

/*! An image that outlives the call that made it: a run crosses threads. */
using InferenceImage = std::shared_ptr<const ImageBuffer>;

/*! One named input or output: an image, or a value (a number, a string, bytes). */
struct InferenceItem {
	Value value;
	InferenceImage image;

	InferenceItem() = default;
	InferenceItem(Value v) : value(std::move(v)) {}
	InferenceItem(InferenceImage i) : image(std::move(i)) {}
	bool isImage() const noexcept { return image != nullptr; }
};

/*! What a run reads. The keys are the engine's vocabulary (`image`, `prompt`, …) and pass
    through an adapter unchanged. */
struct InferenceRequest {
	std::map<std::string, InferenceItem> inputs;
	std::map<std::string, Value> parameters;
};

/*! What a run returns. */
struct InferenceResult {
	std::map<std::string, InferenceItem> outputs;
	/*! The image output of a name, or null. */
	InferenceImage image(std::string_view name) const;
};

/*! A swappable engine. `run` is synchronous by contract. */
class InferenceBackend {
public:
	virtual ~InferenceBackend() = default;
	/*! Whether the backend can serve a run now: its model and resources are loaded. */
	virtual bool isReady() const noexcept = 0;
	/*! A short stable name for the engine, for logging and the cache signature. */
	virtual std::string identifier() const = 0;
	/*! Loads the model so a later run is ready. Called once, off the render thread. */
	virtual Error prepare() { return Error::none(); }
	/*! Runs the model. Fails when the backend is not ready, an input is missing, or the run
	    fails. */
	virtual Error run(const InferenceRequest& request, InferenceResult& result) = 0;
};

/*! The backend with no model: its output image is its input image. An effect renders through
    it until an engine is set, so builds, tests, and timelines work without weights. */
class PassthroughInferenceBackend final : public InferenceBackend {
public:
	explicit PassthroughInferenceBackend(std::string inputName = "image", std::string outputName = "image");
	bool isReady() const noexcept override { return true; }
	std::string identifier() const override { return "passthrough"; }
	Error run(const InferenceRequest& request, InferenceResult& result) override;

private:
	std::string inputName_;
	std::string outputName_;
};

/*! The per-frame outputs of one effect instance. Safe to use from several threads. A stored
    signature that differs from the asked one clears the cache: a new backend or parameter makes
    every stored frame stale. The least recently used frames leave when the byte limit is
    passed. */
class InferenceCache {
public:
	explicit InferenceCache(std::size_t byteLimit = std::size_t{512} * 1024 * 1024);
	~InferenceCache();
	InferenceCache(const InferenceCache&) = delete;
	InferenceCache& operator=(const InferenceCache&) = delete;

	/*! The image stored for a frame under this signature, or null. */
	InferenceImage find(std::int64_t frame, std::string_view signature);
	/*! Stores a frame's image. An image larger than the limit is not kept. */
	void store(std::int64_t frame, std::string_view signature, InferenceImage image);
	void clear();
	std::size_t frameCount() const;
	std::size_t byteCount() const;
	std::size_t byteLimit() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

/*! Runs backends on one thread of its own, in order. */
class InferenceRunner {
public:
	/*! Called on the runner's thread when a run ends, whether or not its caller still waits. */
	using Completion = std::function<void(const Error& error, const InferenceResult& result)>;

	InferenceRunner();
	/*! Finishes the run in progress, cancels the queued ones, and joins the thread. A caller
	    still waiting in `run` for a queued run returns `Cancelled`. */
	~InferenceRunner();
	InferenceRunner(const InferenceRunner&) = delete;
	InferenceRunner& operator=(const InferenceRunner&) = delete;

	/*! Runs `backend` over `request` on the runner's thread and waits for it. `abort` is polled
	    while waiting; when it answers true the call returns `Cancelled` at once and the run
	    carries on, so `completion` can still keep its result. A null backend fails with
	    `InvalidParameter`. */
	Error run(std::shared_ptr<InferenceBackend> backend, InferenceRequest request, InferenceResult& result, const std::function<bool()>& abort = {}, Completion completion = {});
	/*! The number of runs waiting or in progress. */
	std::size_t pendingCount() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

/*! Writes `from` into the `window` of `to`, converting the depth and the component layout, and
    resampling bilinearly when the two differ in size: `from`'s bounds map onto `to`'s. An RGB
    source reads with alpha 1; a one-component destination takes the alpha. */
Error convertInferenceImage(const ImageView& from, const ImageView& to, const RectI& window);

OFXGRIP_NAMESPACE_END
