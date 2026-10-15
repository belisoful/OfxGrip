// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/KernelTile.hpp>

#include <algorithm>

OFXGRIP_NAMESPACE_BEGIN

GripTileMap gripTileMapFor(const RectI& bounds, bool rowsAscend) noexcept
{
	GripTileMap map;
	const float width = static_cast<float>(std::max(bounds.width(), 1));
	const float height = static_cast<float>(std::max(bounds.height(), 1));
	map.u[0] = 1.0f / width;
	map.u[1] = 0.0f;
	map.u[2] = -static_cast<float>(bounds.x1) / width;
	map.u[3] = 0.0f;
	map.v[0] = 0.0f;
	if (rowsAscend) {
		map.v[1] = 1.0f / height;
		map.v[2] = -static_cast<float>(bounds.y1) / height;
	} else {
		map.v[1] = -1.0f / height;
		map.v[2] = static_cast<float>(bounds.y2) / height;
	}
	map.v[3] = 0.0f;
	return map;
}

GripTile makeGripTile(const RectI& destination, Point2D pixelScale, const std::vector<std::optional<RectI>>& textureBounds, bool rowsAscend) noexcept
{
	GripTile tile;
	tile.destinationBounds[0] = static_cast<float>(destination.x1);
	tile.destinationBounds[1] = static_cast<float>(destination.y1);
	tile.destinationBounds[2] = static_cast<float>(destination.x2);
	tile.destinationBounds[3] = static_cast<float>(destination.y2);
	tile.pixelScale[0] = static_cast<float>(pixelScale.x);
	tile.pixelScale[1] = static_cast<float>(pixelScale.y);
	tile.reserved = rowsAscend ? kGripTileRowsAscend : 0u;
	const std::size_t count = std::min(textureBounds.size(), kGripTextureMapLimit);
	tile.textureCount = 0;
	for (std::size_t i = 0; i < count; ++i) {
		if (textureBounds[i].has_value()) {
			tile.textures[i] = gripTileMapFor(*textureBounds[i], rowsAscend);
			tile.textureCount = static_cast<std::uint32_t>(i + 1);
		}
	}
	return tile;
}

bool gripTexelFor(const GripTile& tile, int x, int y, unsigned& texelX, unsigned& texelY) noexcept
{
	const int left = static_cast<int>(tile.destinationBounds[0]);
	const int bottom = static_cast<int>(tile.destinationBounds[1]);
	const int right = static_cast<int>(tile.destinationBounds[2]);
	const int top = static_cast<int>(tile.destinationBounds[3]);
	if (x < left || x >= right || y < bottom || y >= top) {
		return false;
	}
	texelX = static_cast<unsigned>(x - left);
	texelY = static_cast<unsigned>((tile.reserved & kGripTileRowsAscend) != 0 ? y - bottom : top - 1 - y);
	return true;
}

OFXGRIP_NAMESPACE_END
