// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/MetalWatermark.hpp>

#include <ofxgrip/gpu/MetalBackend.hpp>

#include <algorithm>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

namespace metal {

namespace {

struct WatermarkParams {
	int dstX1, dstY1, dstX2, dstY2;
	int winX1, winY1, winX2, winY2;
	int rowBytes;
	int components;
	int colourComponents;
	int pad;
	float colour[4];
	float opacity;
	float pad2[3];
};

constexpr const char* kWatermarkShader = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct WatermarkParams {
    int dstX1, dstY1, dstX2, dstY2;
    int winX1, winY1, winX2, winY2;
    int rowBytes;
    int components;
    int colourComponents;
    int pad;
    float colour[4];
    float opacity;
    float pad2[3];
};

template <typename T> inline float toUnit(T v);
template <> inline float toUnit<uchar>(uchar v) { return float(v) / 255.0f; }
template <> inline float toUnit<ushort>(ushort v) { return float(v) / 65535.0f; }
template <> inline float toUnit<half>(half v) { return float(v); }
template <> inline float toUnit<float>(float v) { return v; }

template <typename T> inline T fromUnit(float v);
template <> inline uchar fromUnit<uchar>(float v) { return uchar(clamp(v * 255.0f + 0.5f, 0.0f, 255.0f)); }
template <> inline ushort fromUnit<ushort>(float v) { return ushort(clamp(v * 65535.0f + 0.5f, 0.0f, 65535.0f)); }
template <> inline half fromUnit<half>(float v) { return half(v); }
template <> inline float fromUnit<float>(float v) { return v; }

template <typename T>
inline void markPixel(device uchar* dst, constant const float* mask, constant WatermarkParams& p, uint2 gid)
{
    const int x = p.winX1 + int(gid.x);
    const int y = p.winY1 + int(gid.y);
    if (x >= p.winX2 || y >= p.winY2) {
        return;
    }
    if (x < p.dstX1 || x >= p.dstX2 || y < p.dstY1 || y >= p.dstY2) {
        return;
    }
    const float a = mask[gid.y * uint(p.winX2 - p.winX1) + gid.x] * p.opacity;
    if (a <= 0.0f) {
        return;
    }
    device T* d = (device T*)(dst + (y - p.dstY1) * p.rowBytes) + (x - p.dstX1) * p.components;
    for (int c = 0; c < p.colourComponents; ++c) {
        d[c] = fromUnit<T>(toUnit<T>(d[c]) * (1.0f - a) + p.colour[c] * a);
    }
}

kernel void watermark_byte(device uchar* dst [[buffer(0)]], constant const float* mask [[buffer(1)]], constant WatermarkParams& p [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) { markPixel<uchar>(dst, mask, p, gid); }
kernel void watermark_short(device uchar* dst [[buffer(0)]], constant const float* mask [[buffer(1)]], constant WatermarkParams& p [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) { markPixel<ushort>(dst, mask, p, gid); }
kernel void watermark_half(device uchar* dst [[buffer(0)]], constant const float* mask [[buffer(1)]], constant WatermarkParams& p [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) { markPixel<half>(dst, mask, p, gid); }
kernel void watermark_float(device uchar* dst [[buffer(0)]], constant const float* mask [[buffer(1)]], constant WatermarkParams& p [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) { markPixel<float>(dst, mask, p, gid); }
)MSL";

const char* kernelNameFor(BitDepth depth) noexcept
{
	switch (depth) {
	case BitDepth::UInt8: return "watermark_byte";
	case BitDepth::UInt16: return "watermark_short";
	case BitDepth::Half: return "watermark_half";
	case BitDepth::Float: return "watermark_float";
	default: return nullptr;
	}
}

}  // namespace

Error metalWatermark(const GpuImage& image, const RectI& window, Point2D frameSize, Point2D frameOrigin, const Watermark& mark)
{
	const char* kernel = kernelNameFor(image.desc.depth);
	if (kernel == nullptr || image.handle == nullptr || image.api != GpuApi::Metal) {
		return Error::make(ErrorCode::Format, "The Metal output frame is unusable for a watermark");
	}
	const RectI area = window.intersect(image.desc.bounds);
	if (area.empty() || image.desc.components == Components::Alpha || mark.configuration().text.empty()) {
		return Error::none();
	}
	std::shared_ptr<MetalContext> backend = MetalContext::forQueue(image.queue);
	if (!backend) {
		return Error::make(ErrorCode::MissingHostFeature, "The host passed no Metal command queue");
	}
	MetalPipeline pipeline;
	if (Error e = backend->pipeline("ofxgrip.watermark", kWatermarkShader, kernel, pipeline)) {
		return e;
	}
	const RectI frameWindow{area.x1 - static_cast<int>(frameOrigin.x), area.y1 - static_cast<int>(frameOrigin.y), area.x2 - static_cast<int>(frameOrigin.x), area.y2 - static_cast<int>(frameOrigin.y)};
	const std::vector<float> mask = mark.mask(frameWindow, frameSize);
	MetalBuffer maskBuffer = backend->newBuffer(mask.size() * sizeof(float), mask.data());
	if (!maskBuffer.valid()) {
		return Error::make(ErrorCode::Memory, "No Metal buffer is available for the watermark mask");
	}
	WatermarkParams params{};
	params.dstX1 = image.desc.bounds.x1;
	params.dstY1 = image.desc.bounds.y1;
	params.dstX2 = image.desc.bounds.x2;
	params.dstY2 = image.desc.bounds.y2;
	params.winX1 = area.x1;
	params.winY1 = area.y1;
	params.winX2 = area.x2;
	params.winY2 = area.y2;
	params.rowBytes = static_cast<int>(image.rowBytes);
	params.components = static_cast<int>(componentCount(image.desc.components));
	params.colourComponents = std::min(params.components, 3);
	const WatermarkConfiguration& configuration = mark.configuration();
	params.colour[0] = static_cast<float>(configuration.color.r);
	params.colour[1] = static_cast<float>(configuration.color.g);
	params.colour[2] = static_cast<float>(configuration.color.b);
	params.colour[3] = 0.0f;
	params.opacity = static_cast<float>(std::clamp(configuration.opacity * configuration.color.a, 0.0, 1.0));

	MetalCommandBuffer command = backend->commandBuffer();
	if (!command.valid()) {
		return Error::make(ErrorCode::Render, "No Metal command buffer is available");
	}
	MetalDispatch dispatch;
	dispatch.pipeline = pipeline;
	dispatch.width = static_cast<unsigned>(area.width());
	dispatch.height = static_cast<unsigned>(area.height());
	dispatch.buffers = {{image.handle, 0, 0}, {maskBuffer.handle(), 0, 1}};
	dispatch.bytes = {{&params, sizeof params, 2}};
	command.retain(std::move(maskBuffer));
	if (Error e = command.dispatch(dispatch)) {
		return e;
	}
	return command.commit();
}

}  // namespace metal

OFXGRIP_NAMESPACE_END
