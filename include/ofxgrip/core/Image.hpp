// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Image.hpp
	@brief CPU image views, owned image buffers, the GPU image descriptor, pixel-format helpers,
	       and the band processor.

	Introduced in OfxGrip 0.1.0. An `ImageView` describes pixels the host owns; an `ImageBuffer`
	owns its pixels. A `GpuImage` is the tagged descriptor `Research/Comparison.md` §3.10
	recommends: the API, the device, the queue, and the handle, with the same layout fields as a
	CPU image. Pixels are bottom-left origin, with `x2` and `y2` exclusive, as OpenFX defines.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The layout of an image. */
struct ImageDesc {
	RectI bounds;                                 /*!< The pixels the data covers. */
	RectI regionOfDefinition;                     /*!< The image's full extent; may exceed bounds. */
	BitDepth depth{BitDepth::Float};
	Components components{Components::RGBA};
	Premultiplication premultiplication{Premultiplication::Premultiplied};
	double pixelAspect{1.0};
	Field field{Field::None};
	Point2D renderScale{1, 1};

	std::size_t pixelBytes() const noexcept { return bytesPerComponent(depth) * componentCount(components); }
	std::size_t minimumRowBytes() const noexcept { return pixelBytes() * static_cast<std::size_t>(bounds.width()); }
	bool operator==(const ImageDesc&) const = default;
};

/*! A view over pixels someone else owns. `rowBytes` may be negative for a top-down CPU image. */
struct ImageView {
	ImageDesc desc;
	std::uint8_t* data{nullptr};
	std::ptrdiff_t rowBytes{0};

	bool valid() const noexcept { return data != nullptr && !desc.bounds.empty(); }
	/*! The address of a pixel inside the bounds, or null outside them. */
	std::uint8_t* pixel(int x, int y) const noexcept;
	/*! A view over a sub-rectangle. The result is empty when the rectangle leaves the bounds. */
	ImageView subView(const RectI& rect) const noexcept;
	/*! Copies the overlapping region of `source` into this view. Layouts must match. */
	Error copyFrom(const ImageView& source) const;
	/*! Fills every pixel in the bounds with zeros. */
	void clear() const noexcept;
};

/*! An image that owns its pixels. */
class ImageBuffer {
public:
	ImageBuffer() = default;
	explicit ImageBuffer(const ImageDesc& desc);
	const ImageDesc& desc() const noexcept { return desc_; }
	ImageView view() noexcept;
	ImageView view() const noexcept;
	std::size_t rowBytes() const noexcept { return rowBytes_; }
	std::vector<std::uint8_t>& bytes() noexcept { return bytes_; }
	const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }

private:
	ImageDesc desc_;
	std::size_t rowBytes_{0};
	std::vector<std::uint8_t> bytes_;
};

/*! The GPU API a frame lives in. `None` is a CPU frame. */
enum class GpuApi : std::uint8_t {
	None = 0,
	Metal,
	Cuda,
	OpenCL,
	OpenGL,
	Vulkan,
	D3D11,
	D3D12,
};

/*! The name of a GPU API. */
std::string_view toString(GpuApi api) noexcept;

/*! A GPU frame. The handle is `id<MTLBuffer>` for Metal, a device pointer for CUDA, a `cl_mem`
    for OpenCL, a texture name for OpenGL. The queue is the host's command queue, stream, or
    command queue, which every kernel must use so host frames are used in place. */
struct GpuImage {
	GpuApi api{GpuApi::None};
	void* device{nullptr};
	void* queue{nullptr};
	void* handle{nullptr};
	ImageDesc desc;
	std::ptrdiff_t rowBytes{0};
	bool needsIntermediateCopy{false};  /*!< Set from the profile (Q-RESOLVE-031). */
};

/*! Reads one component as a float in [0, 1] for integer depths. */
float readComponent(const std::uint8_t* pixel, BitDepth depth, std::size_t component) noexcept;
/*! Writes one component from a float, clamping and rounding for integer depths. */
void writeComponent(std::uint8_t* pixel, BitDepth depth, std::size_t component, float value) noexcept;

/*! Splits a window into horizontal bands and runs `work` on each through the threading
    callback, polling `abort` between bands. Returns `Cancelled` when aborted. */
Error processBands(const RectI& window,
                   unsigned bandCount,
                   const std::function<void(unsigned count, const std::function<void(unsigned index)>& fn)>& runParallel,
                   const std::function<bool()>& abort,
                   const std::function<void(const RectI& band)>& work);

OFXGRIP_NAMESPACE_END
