// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  KernelTile.hpp
	@brief The `GripTile` record a kernel receives and the coordinate maps it holds.

	Introduced in OfxGrip 0.4.0. The layout equals `GripTile` in `kernels/GripKernel.slang` and
	FxGrip's `FxGripShaderTile`: 544 bytes. The framework fills it per dispatch from the
	destination tile and the bounds of every bound texture.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The number of texture indexes a tile carries a coordinate map for. */
inline constexpr std::size_t kGripTextureMapLimit = 16;
/*! The `reserved` flag that says texel row 0 is the bottom row of every texture. */
inline constexpr std::uint32_t kGripTileRowsAscend = 1u;

/*! An affine map from a destination pixel to a normalized coordinate in one texture: `u` and
    `v` hold (x coefficient, y coefficient, constant, 0). */
struct GripTileMap {
	float u[4]{0, 0, 0, 0};
	float v[4]{0, 0, 0, 0};
};

/*! The tile. */
struct GripTile {
	float destinationBounds[4]{0, 0, 0, 0};  /*!< (left, bottom, right, top), y up, destination pixels. */
	float pixelScale[2]{1, 1};               /*!< Destination pixels per image-space unit. */
	std::uint32_t textureCount{0};           /*!< One more than the highest bound texture index. */
	std::uint32_t reserved{0};               /*!< Flags: `kGripTileRowsAscend`. */
	GripTileMap textures[kGripTextureMapLimit];
};
static_assert(sizeof(GripTile) == 544, "GripTile must match the Slang module's layout");
static_assert(sizeof(GripTileMap) == 32, "GripTileMap must match the Slang module's layout");

/*! The map of a texture whose pixels cover `bounds` (y up, exclusive upper edges). With
    `rowsAscend`, normalized v = 0 is the bottom row; otherwise it is the top row. */
GripTileMap gripTileMapFor(const RectI& bounds, bool rowsAscend) noexcept;

/*! Fills a tile. `textureBounds[i]` is the bounds of the texture at map index `i`, or none for
    an unbound index (its map stays zero). */
GripTile makeGripTile(const RectI& destination, Point2D pixelScale, const std::vector<std::optional<RectI>>& textureBounds, bool rowsAscend) noexcept;

/*! The texel of a destination pixel in a tile: the inverse of `gripImagePixel`. */
bool gripTexelFor(const GripTile& tile, int x, int y, unsigned& texelX, unsigned& texelY) noexcept;

OFXGRIP_NAMESPACE_END
