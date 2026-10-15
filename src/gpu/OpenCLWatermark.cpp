// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/OpenCLWatermark.hpp>

#include <ofxgrip/gpu/OpenCLBackend.hpp>

#include <algorithm>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

namespace opencl {

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

constexpr const char* kWatermarkKernel = R"CL(
typedef struct {
    int dstX1, dstY1, dstX2, dstY2;
    int winX1, winY1, winX2, winY2;
    int rowBytes;
    int components;
    int colourComponents;
    int pad;
    float colour[4];
    float opacity;
    float pad2[3];
} WatermarkParams;

#define TO_UNIT_BYTE(ptr) ((float)(*(ptr)) / 255.0f)
#define FROM_UNIT_BYTE(v, ptr) (*(ptr) = (uchar)clamp((v) * 255.0f + 0.5f, 0.0f, 255.0f))
#define TO_UNIT_SHORT(ptr) ((float)(*(ptr)) / 65535.0f)
#define FROM_UNIT_SHORT(v, ptr) (*(ptr) = (ushort)clamp((v) * 65535.0f + 0.5f, 0.0f, 65535.0f))
#define TO_UNIT_HALF(ptr) vload_half(0, (ptr))
#define FROM_UNIT_HALF(v, ptr) vstore_half_rte((v), 0, (ptr))
#define TO_UNIT_FLOAT(ptr) (*(ptr))
#define FROM_UNIT_FLOAT(v, ptr) (*(ptr) = (v))

#define WATERMARK_KERNEL(NAME, T, TO_UNIT, FROM_UNIT)                                                  \
__kernel void NAME(__global uchar* dst, __global const float* mask, WatermarkParams p)              \
{                                                                                                   \
    const int gx = get_global_id(0);                                                                \
    const int gy = get_global_id(1);                                                                \
    const int x = p.winX1 + gx;                                                                     \
    const int y = p.winY1 + gy;                                                                     \
    if (x >= p.winX2 || y >= p.winY2) {                                                             \
        return;                                                                                     \
    }                                                                                               \
    if (x < p.dstX1 || x >= p.dstX2 || y < p.dstY1 || y >= p.dstY2) {                               \
        return;                                                                                     \
    }                                                                                               \
    const float a = mask[gy * (p.winX2 - p.winX1) + gx] * p.opacity;                                \
    if (a <= 0.0f) {                                                                                \
        return;                                                                                     \
    }                                                                                               \
    __global T* d = (__global T*)(dst + (y - p.dstY1) * p.rowBytes) + (x - p.dstX1) * p.components; \
    for (int c = 0; c < p.colourComponents; ++c) {                                                  \
        const float v = TO_UNIT(d + c) * (1.0f - a) + p.colour[c] * a;                              \
        FROM_UNIT(v, d + c);                                                                        \
    }                                                                                               \
}

WATERMARK_KERNEL(watermark_byte, uchar, TO_UNIT_BYTE, FROM_UNIT_BYTE)
WATERMARK_KERNEL(watermark_short, ushort, TO_UNIT_SHORT, FROM_UNIT_SHORT)
WATERMARK_KERNEL(watermark_half, half, TO_UNIT_HALF, FROM_UNIT_HALF)
WATERMARK_KERNEL(watermark_float, float, TO_UNIT_FLOAT, FROM_UNIT_FLOAT)
)CL";

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

Error openCLWatermark(const GpuImage& image, const RectI& window, Point2D frameSize, Point2D frameOrigin, const Watermark& mark)
{
	const char* kernelName = kernelNameFor(image.desc.depth);
	if (kernelName == nullptr || image.handle == nullptr || image.api != GpuApi::OpenCL) {
		return Error::make(ErrorCode::Format, "The OpenCL output frame is unusable for a watermark");
	}
	const RectI area = window.intersect(image.desc.bounds);
	if (area.empty() || image.desc.components == Components::Alpha || mark.configuration().text.empty()) {
		return Error::none();
	}
	std::shared_ptr<OpenCLContext> backend = OpenCLContext::forQueue(image.queue);
	if (!backend) {
		return Error::make(ErrorCode::MissingHostFeature, "The host passed no OpenCL command queue");
	}
	OpenCLKernel kernel;
	if (Error e = backend->kernel("ofxgrip.watermark", kWatermarkKernel, kernelName, kernel)) {
		return e;
	}
	const RectI frameWindow{area.x1 - static_cast<int>(frameOrigin.x), area.y1 - static_cast<int>(frameOrigin.y), area.x2 - static_cast<int>(frameOrigin.x), area.y2 - static_cast<int>(frameOrigin.y)};
	const std::vector<float> mask = mark.mask(frameWindow, frameSize);
	OpenCLBuffer maskBuffer = backend->newBuffer(mask.size() * sizeof(float), mask.data());
	if (!maskBuffer.valid()) {
		return Error::make(ErrorCode::Memory, "No OpenCL buffer is available for the watermark mask");
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

	OpenCLDispatch dispatch;
	dispatch.kernel = kernel;
	dispatch.globalSize[0] = static_cast<std::size_t>(area.width());
	dispatch.globalSize[1] = static_cast<std::size_t>(area.height());
	dispatch.buffers = {{image.handle, 0}, {maskBuffer.handle(), 1}};
	dispatch.bytes = {{&params, sizeof params, 2}};
	// The runtime keeps the mask buffer until the kernel completes; the handle may be released once
	// the kernel is enqueued.
	return backend->enqueue(dispatch);
}

}  // namespace opencl

OFXGRIP_NAMESPACE_END
