// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  InferenceEffect.hpp
	@brief The effect base that renders its source image through an inference backend.

	Introduced in OfxGrip 0.5.0. This is FxGrip's `FxGripMLImageEffect` on OpenFX. A render
	reads the frame's output from the instance's cache, or copies the source frame into a
	request, runs the backend on the instance's runner thread, stores the output, and writes it
	into the render window in the destination's depth and layout. With no engine set the
	passthrough backend renders the source unchanged. A backend that is not ready renders the
	source unchanged too, so a timeline stays usable while a model loads.

	A model reads the whole frame, so an effect built on this base declares CPU rendering with
	tiles off. The render waits for the run and polls the host's abort; an aborted render
	returns at once and the run still finishes into the cache.

	A model that takes seconds per frame also keeps its outputs on disk. `setDiskCacheLimit`
	turns the disk tier on. A render that misses the memory cache then looks the source frame up
	in the folder before it runs the model, and a finished run is written there. The tier is
	off by default: hashing the source and writing the output cost more than a fast model
	saves.

	The cache and the runner are per-instance state behind their own locks. They are the
	inference counterpart of the GPU backends' pipeline cache: a render changes nothing else.
*/
#pragma once

#include <ofxgrip/core/Inference.hpp>
#include <ofxgrip/core/InferenceDiskCache.hpp>
#include <ofxgrip/ofx/Effect.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

OFXGRIP_NAMESPACE_BEGIN

/*! The effect. A subclass names its model's ports and parameters and inherits the pass. */
class InferenceEffect : public Effect {
public:
	InferenceEffect();
	~InferenceEffect() override;

	/*! The full pass: cache, source → request → backend → output → destination. */
	Error render(RenderContext& context) override;

	/*! The engine. Null restores the passthrough. Setting a backend clears the cache through
	    the signature. */
	void setInferenceBackend(std::shared_ptr<InferenceBackend> backend);
	std::shared_ptr<InferenceBackend> inferenceBackend() const;
	/*! Whether a frame's output is kept for the next render of that frame. Default true. */
	void setCacheEnabled(bool enabled) noexcept { cacheEnabled_ = enabled; }
	bool cacheEnabled() const noexcept { return cacheEnabled_; }
	InferenceCache& inferenceCache() noexcept { return cache_; }
	/*! The most bytes the disk tier keeps. Zero, the default, turns the tier off. The first
	    render after a nonzero limit opens the folder `inference` under the plug-in's cache
	    directory, which every instance of the plug-in shares. Introduced in OfxGrip 0.5.0. */
	void setDiskCacheLimit(std::uint64_t bytes);
	std::uint64_t diskCacheLimit() const;
	/*! Sets the disk tier itself: a folder of the effect's choosing. Null with a zero limit
	    turns the tier off. Introduced in OfxGrip 0.5.0. */
	void setDiskCache(std::shared_ptr<InferenceDiskCache> cache);
	/*! The disk tier, or null while it is off or no render has opened it. */
	std::shared_ptr<InferenceDiskCache> diskCache() const;

	/*! The request input the source image goes under. Default `image`. */
	virtual std::string inputImageName() const { return "image"; }
	/*! The result output the destination is read from. Default `image`. */
	virtual std::string outputImageName() const { return "image"; }
	/*! The scalar controls of a frame (seed, strength, a prompt). Default: none. A subclass
	    reads them from the snapshot. */
	virtual std::map<std::string, Value> inferenceParameters(const RenderSnapshot& snapshot) const;
	/*! What a stored frame depends on beyond its time: by default the backend's identifier,
	    the render scale, and the frame's parameters. */
	virtual std::string cacheSignature(const RenderSnapshot& snapshot, const InferenceBackend& backend) const;

private:
	mutable std::mutex mutex_;
	std::shared_ptr<InferenceBackend> backend_;
	std::shared_ptr<InferenceDiskCache> diskTier(HostServices& services);
	bool cacheEnabled_{true};
	std::uint64_t diskLimit_{0};
	std::shared_ptr<InferenceDiskCache> disk_;
	InferenceCache cache_;
	InferenceRunner runner_;
};

OFXGRIP_NAMESPACE_END
