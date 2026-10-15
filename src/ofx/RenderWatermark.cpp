// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/RenderWatermark.hpp>

#if defined(OFXGRIP_HAS_METAL)
#include <ofxgrip/gpu/MetalWatermark.hpp>
#endif
#if defined(OFXGRIP_HAS_OPENCL)
#include <ofxgrip/gpu/OpenCLWatermark.hpp>
#endif

OFXGRIP_NAMESPACE_BEGIN

Error applyRenderWatermark(RenderContext& context, const WatermarkConfiguration& configuration)
{
	if (configuration.text.empty()) {
		return Error::none();
	}
	const Watermark mark(configuration);
	const ImageDesc& desc = context.outputIsGpu() ? context.outputGpu.desc : context.output.desc;
	RectI frame = desc.regionOfDefinition;
	if (frame.empty()) {
		frame = desc.bounds;
	}
	const Point2D frameSize{static_cast<double>(frame.width()), static_cast<double>(frame.height())};
	const Point2D frameOrigin{static_cast<double>(frame.x1), static_cast<double>(frame.y1)};
	RectI window = context.args.renderWindow;
	if (window.empty()) {
		window = desc.bounds;
	}
	window = window.intersect(desc.bounds);
	if (window.empty()) {
		return Error::none();
	}
	if (!context.outputIsGpu()) {
		mark.composite(context.output, window, frameSize, frameOrigin);
		return Error::none();
	}
	switch (context.outputGpu.api) {
#if defined(OFXGRIP_HAS_METAL)
	case GpuApi::Metal:
		return metal::metalWatermark(context.outputGpu, window, frameSize, frameOrigin, mark);
#endif
#if defined(OFXGRIP_HAS_OPENCL)
	case GpuApi::OpenCL:
		return opencl::openCLWatermark(context.outputGpu, window, frameSize, frameOrigin, mark);
#endif
	default:
		return Error::make(ErrorCode::Unsupported, "The watermark has no path for this GPU API").with("api", std::string(toString(context.outputGpu.api)));
	}
}

OFXGRIP_NAMESPACE_END
