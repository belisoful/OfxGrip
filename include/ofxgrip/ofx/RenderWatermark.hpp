// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  RenderWatermark.hpp
	@brief Applies the licensing watermark to a render's output after the effect has rendered.

	Introduced in OfxGrip 0.4.0. The adapter calls this once per render when the licensing
	extension asks for a mark. CPU frames composite in place; Metal and OpenCL frames run the
	backend's blend kernel on the host's queue. Other GPU APIs report `Unsupported`, which fails
	the render: an unlicensed frame never leaves without its mark.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Watermark.hpp>
#include <ofxgrip/ofx/Effect.hpp>

OFXGRIP_NAMESPACE_BEGIN

/*! Composites `configuration` over the render window of `context`'s output. The mark is placed
    on the whole frame (the output's region of definition), so tiles of one frame line up. */
Error applyRenderWatermark(RenderContext& context, const WatermarkConfiguration& configuration);

OFXGRIP_NAMESPACE_END
