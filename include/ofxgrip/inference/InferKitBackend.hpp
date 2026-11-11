// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  InferKitBackend.hpp
	@brief An inference backend over InferKit's C interface (`NFKInferKitC.h`, InferKit 0.4.0).

	Introduced in OfxGrip 0.5.0. The InferKit library is opened at run time from a path the
	plug-in supplies, so OfxGrip links no engine and a plug-in loads without one. A request
	crosses as InferKit's tagged JSON and buffers:

	- An image input → a `pixelBuffer` buffer of four float channels (`'RGfA'`), top row first.
	- A number, a boolean, or a string → itself. A point or a color → an array of numbers.
	- Bytes → a `data` buffer.

	An image output comes back as a `pixelBuffer` of four float channels, four half channels,
	8-bit RGBA or BGRA, or one float channel, and becomes an image with the bottom row first. A
	PNG-encoded image output (`cgImage`, `image`) is refused: OfxGrip carries no PNG decoder. A
	`float32Array`, `float64Array`, or `data` buffer becomes bytes. Any other output becomes its
	JSON text.
*/
#pragma once

#include <ofxgrip/core/Inference.hpp>

#include <memory>
#include <string>

OFXGRIP_NAMESPACE_BEGIN

/*! The backend over one InferKit catalog model. */
class InferKitBackend final : public InferenceBackend {
public:
	~InferKitBackend() override;
	/*! Opens the InferKit C library at `libraryPath` and builds the catalog model
	    `catalogIdentifier` with `optionsJson` (a JSON object, or empty for none). Fails with
	    `MissingHostFeature` when the library does not open or lacks the interface, and with
	    InferKit's own error when the model cannot be built. */
	static Error create(const std::string& libraryPath, const std::string& catalogIdentifier, const std::string& optionsJson, std::shared_ptr<InferKitBackend>& out);

	/*! True once `prepare` has loaded the model. */
	bool isReady() const noexcept override;
	/*! `inferkit:<catalog identifier>`. */
	std::string identifier() const override;
	Error prepare() override;
	Error run(const InferenceRequest& request, InferenceResult& result) override;
	/*! InferKit's interface version, `(major << 16) | minor`. */
	std::uint32_t apiVersion() const noexcept;

private:
	struct Impl;
	explicit InferKitBackend(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};

OFXGRIP_NAMESPACE_END
