// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The texel conversions the backends that work on CPU frames share. A kernel sees float RGBA:
// an RGB frame reads with alpha 1, and a one-component frame reads as (a, 0, 0, 1), as a Metal
// `R` format does. A one-component destination takes the kernel's alpha.
#pragma once

#include <ofxgrip/core/Image.hpp>

#include <array>

OFXGRIP_NAMESPACE_BEGIN

inline std::array<float, 4> kernelTexel(const ImageView& view, int x, int y) noexcept
{
	const std::uint8_t* pixel = view.pixel(x, y);
	if (pixel == nullptr) {
		return {0.0f, 0.0f, 0.0f, 0.0f};
	}
	const BitDepth depth = view.desc.depth;
	switch (view.desc.components) {
	case Components::RGBA:
		return {readComponent(pixel, depth, 0), readComponent(pixel, depth, 1), readComponent(pixel, depth, 2), readComponent(pixel, depth, 3)};
	case Components::RGB:
		return {readComponent(pixel, depth, 0), readComponent(pixel, depth, 1), readComponent(pixel, depth, 2), 1.0f};
	case Components::Alpha:
		return {readComponent(pixel, depth, 0), 0.0f, 0.0f, 1.0f};
	case Components::None:
		break;
	}
	return {0.0f, 0.0f, 0.0f, 0.0f};
}

inline void writeKernelTexel(const ImageView& out, int x, int y, const float* texel) noexcept
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

OFXGRIP_NAMESPACE_END
