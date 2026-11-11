// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  CudaWatermark.hpp
	@brief The licensing watermark on a CUDA frame.

	Introduced in OfxGrip 0.5.0. The window is copied to host memory, composited by the CPU
	path, and copied back, after the stream has drained. The mark is therefore identical to the
	CPU frame's in every depth and layout, at the cost of two copies on unlicensed renders.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Image.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Watermark.hpp>
#include <ofxgrip/gpu/CudaDriver.hpp>

OFXGRIP_NAMESPACE_BEGIN

namespace cuda {

/*! Blends `mark` over `window` of a CUDA frame. `frameSize` and `frameOrigin` place the mark
    on the whole frame the window belongs to. A null `driver` is the machine's driver. */
Error cudaWatermark(const GpuImage& image, const RectI& window, Point2D frameSize, Point2D frameOrigin, const Watermark& mark, const CudaDriver* driver = nullptr);

}  // namespace cuda

OFXGRIP_NAMESPACE_END
