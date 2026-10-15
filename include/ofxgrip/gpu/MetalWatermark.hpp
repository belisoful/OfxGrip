// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  MetalWatermark.hpp
	@brief Composites a watermark over a Metal output frame.

	Introduced in OfxGrip 0.4.0. The coverage mask is computed on the CPU for the window and
	uploaded once; a small blend kernel applies it to the colour components in place, at the
	frame's depth. The command buffer commits without waiting, as `ofxGPURender.h` asks.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Image.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Watermark.hpp>

OFXGRIP_NAMESPACE_BEGIN

namespace metal {

/*! Blends `mark` over `window` of a Metal `MTLBuffer` frame. `frameSize` and `frameOrigin`
    place the mark on the whole frame the window belongs to. */
Error metalWatermark(const GpuImage& image, const RectI& window, Point2D frameSize, Point2D frameOrigin, const Watermark& mark);

}  // namespace metal

OFXGRIP_NAMESPACE_END
