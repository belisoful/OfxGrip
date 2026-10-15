// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/gpu/CpuKernelBackend.hpp>

#include <ofxgrip/core/KernelTile.hpp>

#include <slang-cpp-prelude.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

namespace {

using Float4 = Vector<float, 4>;

/*! Reads a CPU view as a texture: bilinear clamp-to-edge sampling with normalized coordinates,
    row 0 at the bottom (the OpenFX orientation, `kGripTileRowsAscend`). A disconnected image
    reads transparent black. */
class ViewTexture final : public ITexture {
public:
	explicit ViewTexture(const ImageView* view) : view_(view != nullptr && view->valid() ? view : nullptr) {}

	TextureDimensions GetDimensions(int) override
	{
		TextureDimensions dims;
		dims.reset();
		dims.shape = SLANG_TEXTURE_2D;
		dims.width = view_ ? static_cast<uint32_t>(view_->desc.bounds.width()) : 1u;
		dims.height = view_ ? static_cast<uint32_t>(view_->desc.bounds.height()) : 1u;
		dims.depth = 1;
		dims.numberOfLevels = 1;
		return dims;
	}

	void Load(const int32_t* v, void* outData, size_t dataSize) override
	{
		Float4 texel{0.0f, 0.0f, 0.0f, 0.0f};
		if (view_ != nullptr) {
			const RectI& b = view_->desc.bounds;
			const int x = b.x1 + v[0];
			const int y = b.y1 + v[1];
			if (b.contains(x, y)) {
				texel = fetch(x, y);
			}
		}
		std::memcpy(outData, &texel, std::min(dataSize, sizeof texel));
	}

	void Sample(SamplerState, const float* loc, void* outData, size_t dataSize) override
	{
		const Float4 texel = sample(loc[0], loc[1]);
		std::memcpy(outData, &texel, std::min(dataSize, sizeof texel));
	}

	void SampleLevel(SamplerState, const float* loc, float, void* outData, size_t dataSize) override
	{
		const Float4 texel = sample(loc[0], loc[1]);
		std::memcpy(outData, &texel, std::min(dataSize, sizeof texel));
	}

private:
	Float4 fetch(int x, int y) const noexcept
	{
		const std::uint8_t* pixel = view_->pixel(x, y);
		if (pixel == nullptr) {
			return Float4{0.0f, 0.0f, 0.0f, 0.0f};
		}
		const BitDepth depth = view_->desc.depth;
		switch (view_->desc.components) {
		case Components::RGBA:
			return Float4{readComponent(pixel, depth, 0), readComponent(pixel, depth, 1), readComponent(pixel, depth, 2), readComponent(pixel, depth, 3)};
		case Components::RGB:
			return Float4{readComponent(pixel, depth, 0), readComponent(pixel, depth, 1), readComponent(pixel, depth, 2), 1.0f};
		case Components::Alpha:
			return Float4{readComponent(pixel, depth, 0), 0.0f, 0.0f, 1.0f};
		case Components::None:
			break;
		}
		return Float4{0.0f, 0.0f, 0.0f, 0.0f};
	}

	Float4 sample(float u, float v) const noexcept
	{
		if (view_ == nullptr) {
			return Float4{0.0f, 0.0f, 0.0f, 0.0f};
		}
		const RectI& b = view_->desc.bounds;
		const float width = static_cast<float>(b.width());
		const float height = static_cast<float>(b.height());
		// Texel centers sit at half-integers; clamp-to-edge holds the edge texel outside.
		const float fx = std::clamp(u * width - 0.5f, 0.0f, width - 1.0f);
		const float fy = std::clamp(v * height - 0.5f, 0.0f, height - 1.0f);
		const int x0 = static_cast<int>(std::floor(fx));
		const int y0 = static_cast<int>(std::floor(fy));
		const int x1 = std::min(x0 + 1, b.width() - 1);
		const int y1 = std::min(y0 + 1, b.height() - 1);
		const float tx = fx - static_cast<float>(x0);
		const float ty = fy - static_cast<float>(y0);
		const Float4 a = fetch(b.x1 + x0, b.y1 + y0);
		const Float4 c = fetch(b.x1 + x1, b.y1 + y0);
		const Float4 d = fetch(b.x1 + x0, b.y1 + y1);
		const Float4 e = fetch(b.x1 + x1, b.y1 + y1);
		Float4 out;
		for (int i = 0; i < 4; ++i) {
			const float top = a[static_cast<size_t>(i)] + (c[static_cast<size_t>(i)] - a[static_cast<size_t>(i)]) * tx;
			const float bottom = d[static_cast<size_t>(i)] + (e[static_cast<size_t>(i)] - d[static_cast<size_t>(i)]) * tx;
			out[static_cast<size_t>(i)] = top + (bottom - top) * ty;
		}
		return out;
	}

	const ImageView* view_;
};

/*! The destination: a float tile the kernel writes through `refAt`. */
class TileTexture final : public IRWTexture {
public:
	TileTexture(unsigned width, unsigned height) : width_(width), height_(height), texels_(static_cast<std::size_t>(width) * height) {}

	TextureDimensions GetDimensions(int) override
	{
		TextureDimensions dims;
		dims.reset();
		dims.shape = SLANG_TEXTURE_2D;
		dims.width = width_;
		dims.height = height_;
		dims.depth = 1;
		dims.numberOfLevels = 1;
		return dims;
	}
	void Load(const int32_t* v, void* outData, size_t dataSize) override
	{
		Float4 texel{0.0f, 0.0f, 0.0f, 0.0f};
		if (v[0] >= 0 && v[1] >= 0 && static_cast<unsigned>(v[0]) < width_ && static_cast<unsigned>(v[1]) < height_) {
			texel = texels_[static_cast<std::size_t>(v[1]) * width_ + static_cast<std::size_t>(v[0])];
		}
		std::memcpy(outData, &texel, std::min(dataSize, sizeof texel));
	}
	void Sample(SamplerState, const float*, void* outData, size_t dataSize) override
	{
		const Float4 texel{0.0f, 0.0f, 0.0f, 0.0f};
		std::memcpy(outData, &texel, std::min(dataSize, sizeof texel));
	}
	void SampleLevel(SamplerState s, const float* loc, float, void* outData, size_t dataSize) override { Sample(s, loc, outData, dataSize); }
	void* refAt(const uint32_t* loc) override
	{
		if (loc[0] >= width_ || loc[1] >= height_) {
			return &overflow_;
		}
		return &texels_[static_cast<std::size_t>(loc[1]) * width_ + loc[0]];
	}
	const Float4& at(unsigned x, unsigned y) const noexcept { return texels_[static_cast<std::size_t>(y) * width_ + x]; }

private:
	unsigned width_;
	unsigned height_;
	std::vector<Float4> texels_;
	Float4 overflow_{};
};

struct VaryingInput {
	uint32_t startGroup[3];
	uint32_t endGroup[3];
};
static_assert(sizeof(VaryingInput) == sizeof(ComputeVaryingInput), "The varying input must match the prelude's layout");

void writeTexel(const ImageView& out, int x, int y, const Float4& texel) noexcept
{
	std::uint8_t* pixel = out.pixel(x, y);
	if (pixel == nullptr) {
		return;
	}
	const std::size_t n = componentCount(out.desc.components);
	for (std::size_t c = 0; c < n; ++c) {
		const float value = out.desc.components == Components::Alpha ? texel[3] : texel[c];
		writeComponent(pixel, out.desc.depth, c, value);
	}
}

}  // namespace

Error CpuKernelBackend::dispatch(const KernelDispatch& d)
{
	const KernelLayout& layout = *d.layout;
	const KernelImplementation& impl = *d.implementation;
	if (impl.cpuEntry == nullptr) {
		return Error::make(ErrorCode::InvalidParameter, "The kernel implementation has no CPU entry");
	}
	if (layout.form() != KernelBindingForm::Block) {
		return Error::make(ErrorCode::InvalidParameter, "The CPU backend needs the C++ target's block layout");
	}
	if (d.destination.isGpu() || !d.destination.cpu.valid()) {
		return Error::make(ErrorCode::InvalidParameter, "The CPU backend writes a CPU destination");
	}
	const ImageView& out = d.destination.cpu;
	const RectI window = d.window.empty() ? out.desc.bounds : d.window.intersect(out.desc.bounds);
	if (window.empty()) {
		return Error::none();
	}

	// The textures, in map order, and the tile over their bounds.
	std::vector<std::unique_ptr<ViewTexture>> textures;
	std::vector<std::optional<RectI>> bounds(layout.textureCount());
	std::vector<std::pair<std::size_t, ITexture*>> textureSlots;
	for (const KernelBindingPlan::Texture& bound : d.plan->textures) {
		const KernelImage* image = kernelImageFor(d, bound);
		const ImageView* view = image != nullptr && image->connected && !image->isGpu() ? &image->cpu : nullptr;
		if (image != nullptr && image->connected && image->isGpu()) {
			return Error::make(ErrorCode::InvalidParameter, "The CPU backend reads CPU sources").with("texture", bound.name);
		}
		textures.push_back(std::make_unique<ViewTexture>(view));
		if (view != nullptr && bound.mapIndex < bounds.size()) {
			bounds[bound.mapIndex] = view->desc.bounds;
		}
		const KernelTexture* texture = layout.textureNamed(bound.name);
		textureSlots.emplace_back(texture->blockOffset, textures.back().get());
	}
	const KernelTexture& destination = layout.destination();
	if (destination.mapIndex < bounds.size()) {
		bounds[destination.mapIndex] = window;
	}
	TileTexture tile(static_cast<unsigned>(window.width()), static_cast<unsigned>(window.height()));
	const GripTile gripTile = makeGripTile(window, d.pixelScale, bounds, true);

	// The globals block: pointers, constants, and the sampler the prelude passes by value.
	Bytes globals(std::max<std::size_t>(layout.globalsLength(), 1), 0);
	auto put = [&globals](std::size_t offset, const void* data, std::size_t size) {
		if (offset + size <= globals.size()) {
			std::memcpy(globals.data() + offset, data, size);
		}
	};
	const std::uint8_t* uniformsPointer = d.uniforms.data();
	if (layout.hasUniforms()) {
		put(layout.uniformsOffset(), &uniformsPointer, sizeof uniformsPointer);
	}
	const GripTile* tilePointer = &gripTile;
	if (layout.hasTile()) {
		put(layout.tileOffset(), &tilePointer, sizeof tilePointer);
	}
	for (const auto& [offset, texture] : textureSlots) {
		ITexture* pointer = texture;
		put(offset, &pointer, sizeof pointer);
	}
	IRWTexture* destinationPointer = &tile;
	put(destination.blockOffset, &destinationPointer, sizeof destinationPointer);
	for (const KernelSampler& sampler : layout.samplers()) {
		const void* none = nullptr;
		put(sampler.blockOffset, &none, sizeof none);
	}
	for (const KernelConstantValue& constant : d.constants) {
		put(constant.blockOffset, constant.bytes.data(), constant.length);
	}

	// The grid, split into bands of threadgroup rows across the threads.
	const std::array<unsigned, 3>& group = layout.threadGroupSize();
	const unsigned groupsX = (static_cast<unsigned>(window.width()) + group[0] - 1) / group[0];
	const unsigned groupsY = (static_cast<unsigned>(window.height()) + group[1] - 1) / group[1];
	auto entry = reinterpret_cast<CpuKernelEntry>(impl.cpuEntry);
	std::atomic<bool> aborted{false};
	auto runBand = [&](unsigned firstRow, unsigned endRow) {
		if (firstRow >= endRow) {
			return;
		}
		if (d.abort && d.abort()) {
			aborted = true;
			return;
		}
		VaryingInput input{{0, firstRow, 0}, {groupsX, endRow, 1}};
		entry(&input, nullptr, globals.data());
	};
	unsigned bands = 1;
	if (d.threading != nullptr) {
		bands = std::max(1u, std::min(groupsY, d.threading->cpuCount() * 2));
	}
	if (bands <= 1) {
		runBand(0, groupsY);
	} else {
		const unsigned rowsPerBand = (groupsY + bands - 1) / bands;
		d.threading->parallelFor(bands, [&](unsigned index, unsigned) {
			const unsigned first = index * rowsPerBand;
			runBand(first, std::min(groupsY, first + rowsPerBand));
		});
	}
	if (aborted) {
		return Error::cancelled();
	}
	// The float tile converts into the destination's depth.
	for (int y = window.y1; y < window.y2; ++y) {
		for (int x = window.x1; x < window.x2; ++x) {
			writeTexel(out, x, y, tile.at(static_cast<unsigned>(x - window.x1), static_cast<unsigned>(y - window.y1)));
		}
	}
	return Error::none();
}

OFXGRIP_NAMESPACE_END
