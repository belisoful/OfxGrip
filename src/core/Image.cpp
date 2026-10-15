// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Image.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

OFXGRIP_NAMESPACE_BEGIN

std::string_view toString(GpuApi api) noexcept
{
	switch (api) {
	case GpuApi::None: return "cpu";
	case GpuApi::Metal: return "metal";
	case GpuApi::Cuda: return "cuda";
	case GpuApi::OpenCL: return "opencl";
	case GpuApi::OpenGL: return "opengl";
	case GpuApi::Vulkan: return "vulkan";
	case GpuApi::D3D11: return "d3d11";
	case GpuApi::D3D12: return "d3d12";
	}
	return "cpu";
}

std::uint8_t* ImageView::pixel(int x, int y) const noexcept
{
	if (data == nullptr || !desc.bounds.contains(x, y)) {
		return nullptr;
	}
	const std::ptrdiff_t row = static_cast<std::ptrdiff_t>(y - desc.bounds.y1) * rowBytes;
	const std::ptrdiff_t col = static_cast<std::ptrdiff_t>(x - desc.bounds.x1) * static_cast<std::ptrdiff_t>(desc.pixelBytes());
	return data + row + col;
}

ImageView ImageView::subView(const RectI& rect) const noexcept
{
	ImageView out = *this;
	const RectI clipped = rect.intersect(desc.bounds);
	if (clipped.empty() || data == nullptr) {
		out.data = nullptr;
		out.desc.bounds = clipped;
		return out;
	}
	out.data = pixel(clipped.x1, clipped.y1);
	out.desc.bounds = clipped;
	return out;
}

Error ImageView::copyFrom(const ImageView& source) const
{
	if (!valid() || !source.valid()) {
		return Error::make(ErrorCode::Value, "Cannot copy an empty image");
	}
	if (source.desc.depth != desc.depth || source.desc.components != desc.components) {
		return Error::make(ErrorCode::Format, "Image layouts differ");
	}
	const RectI overlap = desc.bounds.intersect(source.desc.bounds);
	if (overlap.empty()) {
		return Error::none();
	}
	const std::size_t rowLength = static_cast<std::size_t>(overlap.width()) * desc.pixelBytes();
	for (int y = overlap.y1; y < overlap.y2; ++y) {
		std::memcpy(pixel(overlap.x1, y), source.pixel(overlap.x1, y), rowLength);
	}
	return Error::none();
}

void ImageView::clear() const noexcept
{
	if (!valid()) {
		return;
	}
	const std::size_t rowLength = desc.minimumRowBytes();
	for (int y = desc.bounds.y1; y < desc.bounds.y2; ++y) {
		std::memset(pixel(desc.bounds.x1, y), 0, rowLength);
	}
}

ImageBuffer::ImageBuffer(const ImageDesc& desc) : desc_(desc)
{
	rowBytes_ = desc.minimumRowBytes();
	// Align rows to 16 bytes so SIMD kernels and Metal buffer aliasing see a friendly pitch.
	rowBytes_ = (rowBytes_ + 15) & ~static_cast<std::size_t>(15);
	bytes_.assign(rowBytes_ * static_cast<std::size_t>(desc.bounds.height()), 0);
}

ImageView ImageBuffer::view() noexcept
{
	ImageView v;
	v.desc = desc_;
	v.data = bytes_.empty() ? nullptr : bytes_.data();
	v.rowBytes = static_cast<std::ptrdiff_t>(rowBytes_);
	return v;
}

ImageView ImageBuffer::view() const noexcept
{
	ImageView v;
	v.desc = desc_;
	v.data = bytes_.empty() ? nullptr : const_cast<std::uint8_t*>(bytes_.data());
	v.rowBytes = static_cast<std::ptrdiff_t>(rowBytes_);
	return v;
}

namespace {

}  // namespace

float halfToFloat(std::uint16_t h) noexcept
{
	const std::uint32_t sign = static_cast<std::uint32_t>(h >> 15) << 31;
	std::uint32_t exponent = (h >> 10) & 0x1F;
	std::uint32_t mantissa = h & 0x3FF;
	std::uint32_t bits = 0;
	if (exponent == 0) {
		if (mantissa == 0) {
			bits = sign;
		} else {
			exponent = 127 - 15 + 1;
			while ((mantissa & 0x400) == 0) {
				mantissa <<= 1;
				--exponent;
			}
			mantissa &= 0x3FF;
			bits = sign | (exponent << 23) | (mantissa << 13);
		}
	} else if (exponent == 0x1F) {
		bits = sign | 0x7F800000u | (mantissa << 13);
	} else {
		bits = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
	}
	float f = 0;
	std::memcpy(&f, &bits, sizeof f);
	return f;
}

std::uint16_t floatToHalf(float f) noexcept
{
	std::uint32_t bits = 0;
	std::memcpy(&bits, &f, sizeof bits);
	const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000);
	std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFF) - 127 + 15;
	std::uint32_t mantissa = bits & 0x7FFFFF;
	if (exponent >= 0x1F) {
		return static_cast<std::uint16_t>(sign | 0x7C00);
	}
	if (exponent <= 0) {
		if (exponent < -10) {
			return sign;
		}
		mantissa |= 0x800000;
		const std::uint32_t shift = static_cast<std::uint32_t>(14 - exponent);
		std::uint32_t half = mantissa >> shift;
		const std::uint32_t remainder = mantissa & ((1u << shift) - 1);
		if (remainder > (1u << (shift - 1)) || (remainder == (1u << (shift - 1)) && (half & 1))) {
			++half;
		}
		return static_cast<std::uint16_t>(sign | half);
	}
	std::uint32_t half = static_cast<std::uint32_t>(exponent << 10) | (mantissa >> 13);
	const std::uint32_t remainder = mantissa & 0x1FFF;
	if (remainder > 0x1000 || (remainder == 0x1000 && (half & 1))) {
		++half;
	}
	return static_cast<std::uint16_t>(sign | half);
}

float readComponent(const std::uint8_t* pixel, BitDepth depth, std::size_t component) noexcept
{
	switch (depth) {
	case BitDepth::UInt8:
		return static_cast<float>(pixel[component]) / 255.0f;
	case BitDepth::UInt16: {
		std::uint16_t v = 0;
		std::memcpy(&v, pixel + component * 2, 2);
		return static_cast<float>(v) / 65535.0f;
	}
	case BitDepth::Half: {
		std::uint16_t v = 0;
		std::memcpy(&v, pixel + component * 2, 2);
		return halfToFloat(v);
	}
	case BitDepth::Float: {
		float v = 0;
		std::memcpy(&v, pixel + component * 4, 4);
		return v;
	}
	case BitDepth::None:
		break;
	}
	return 0.0f;
}

void writeComponent(std::uint8_t* pixel, BitDepth depth, std::size_t component, float value) noexcept
{
	switch (depth) {
	case BitDepth::UInt8: {
		const float clamped = std::clamp(value, 0.0f, 1.0f);
		pixel[component] = static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
		break;
	}
	case BitDepth::UInt16: {
		const float clamped = std::clamp(value, 0.0f, 1.0f);
		const std::uint16_t v = static_cast<std::uint16_t>(std::lround(clamped * 65535.0f));
		std::memcpy(pixel + component * 2, &v, 2);
		break;
	}
	case BitDepth::Half: {
		const std::uint16_t v = floatToHalf(value);
		std::memcpy(pixel + component * 2, &v, 2);
		break;
	}
	case BitDepth::Float:
		std::memcpy(pixel + component * 4, &value, 4);
		break;
	case BitDepth::None:
		break;
	}
}

Error processBands(const RectI& window,
                   unsigned bandCount,
                   const std::function<void(unsigned count, const std::function<void(unsigned index)>& fn)>& runParallel,
                   const std::function<bool()>& abort,
                   const std::function<void(const RectI& band)>& work)
{
	if (window.empty()) {
		return Error::none();
	}
	const unsigned height = static_cast<unsigned>(window.height());
	const unsigned count = std::max(1u, std::min(bandCount, height));
	std::atomic<bool> aborted{false};
	runParallel(count, [&](unsigned index) {
		if (aborted.load(std::memory_order_relaxed)) {
			return;
		}
		if (abort && abort()) {
			aborted.store(true, std::memory_order_relaxed);
			return;
		}
		const int y1 = window.y1 + static_cast<int>(height * index / count);
		const int y2 = window.y1 + static_cast<int>(height * (index + 1) / count);
		if (y2 <= y1) {
			return;
		}
		work(RectI{window.x1, y1, window.x2, y2});
	});
	if (aborted.load() || (abort && abort())) {
		return Error::cancelled();
	}
	return Error::none();
}

OFXGRIP_NAMESPACE_END
