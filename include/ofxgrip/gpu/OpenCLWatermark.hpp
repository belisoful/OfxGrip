// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  OpenCLWatermark.hpp
	@brief Composites a watermark over an OpenCL output frame.

	Introduced in OfxGrip 0.4.0. The coverage mask is computed on the CPU for the window and
	uploaded once; a small blend kernel applies it to the colour components in place, at the
	frame's depth. The kernel enqueues without waiting, as `ofxGPURender.h` asks.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Image.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Watermark.hpp>

OFXGRIP_NAMESPACE_BEGIN

namespace opencl {

/*! Blends `mark` over `window` of an OpenCL `cl_mem` frame. `frameSize` and `frameOrigin`
    place the mark on the whole frame the window belongs to. */
Error openCLWatermark(const GpuImage& image, const RectI& window, Point2D frameSize, Point2D frameOrigin, const Watermark& mark);

}  // namespace opencl

OFXGRIP_NAMESPACE_END
