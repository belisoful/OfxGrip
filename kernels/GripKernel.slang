// GripKernel.slang
// Copyright © 2026 Belisoful. MIT License.
//
// The kernel contract FxGrip and OfxGrip share (D-009). A kernel imports this module, declares its
// globals, and defines one compute entry point. The framework binds the globals by role and name
// from the reflection JSON slangc emits, so the names in this module are part of the contract.
// GripTile is 544 bytes, the layout of FxGrip's FxGripShaderTile, and both frameworks fill it the
// same way.
//
// OfxGrip's addition to the FxGrip module of commit 518e6d9: the `reserved` word carries flags.
// Bit 0 (kGripTileRowsAscend) says that texel row 0 is the bottom row of every texture, which is
// how an OpenFX host's bottom-up frame buffer reads when it is wrapped as a texture. FxGrip's
// textures are top-down and FxGrip leaves the word 0, so a kernel built against either copy of
// this module computes the same destination pixel for a texel.
//
// Build a kernel against this module with scripts/ofxgrip-slang.sh, which runs:
//   slangc -I <directory of this file> <kernel>.slang -target metal -entry <name> -stage compute
//          -line-directive-mode standard -reflection-json <kernel>.json -o <kernel>.metal
module GripKernel;

/// The number of texture indexes a tile carries a coordinate map for.
public static const uint kGripTextureMapLimit = 16;

/// Texel row 0 is the bottom row (OpenFX frame buffers). Clear → row 0 is the top row (FxPlug).
public static const uint kGripTileRowsAscend = 1;

/// An affine map from a destination pixel to a normalized coordinate in one texture.
/// u and v each hold the map's row as (x coefficient, y coefficient, constant, 0).
public struct GripTileMap
{
    public float4 u;
    public float4 v;
};

/// The destination tile.
/// - destinationBounds → the tile's bounds in destination pixel space as (left, bottom, right, top), y up.
/// - pixelScale → the destination's pixels per image-space unit along x and y; below 1 at a proxy resolution.
/// - textureCount → one more than the highest bound texture index.
/// - reserved → flags: kGripTileRowsAscend.
/// - textures → the coordinate map of each texture index below kGripTextureMapLimit.
public struct GripTile
{
    public float4 destinationBounds;
    public float2 pixelScale;
    public uint textureCount;
    public uint reserved;
    public GripTileMap textures[kGripTextureMapLimit];
};

/// The size of the destination tile in texels.
public uint2 gripTileSize(GripTile tile)
{
    float4 d = tile.destinationBounds;
    return uint2(uint(d.z - d.x), uint(d.w - d.y));
}

/// True when a texel lies inside the destination tile. The framework dispatches whole
/// threadgroups, so a kernel returns early when this is false.
public bool gripInsideTile(GripTile tile, uint2 texel)
{
    uint2 size = gripTileSize(tile);
    return texel.x < size.x && texel.y < size.y;
}

/// The center of a destination texel in destination pixel space, y up: the space of the tile's
/// destinationBounds. Texel row 0 is the top row unless the tile sets kGripTileRowsAscend.
public float2 gripImagePixel(GripTile tile, uint2 texel)
{
    float4 d = tile.destinationBounds;
    if ((tile.reserved & kGripTileRowsAscend) != 0)
    {
        return float2(d.x + float(texel.x) + 0.5, d.y + float(texel.y) + 0.5);
    }
    return float2(d.x + float(texel.x) + 0.5, d.w - float(texel.y) - 0.5);
}

/// The normalized coordinate in texture `index` of the destination pixel at `imagePixel`.
/// `index` is the texture's binding index. An index at or past kGripTextureMapLimit maps to (0, 0).
public float2 gripTexCoord(GripTile tile, uint index, float2 imagePixel)
{
    if (index >= kGripTextureMapLimit)
    {
        return float2(0.0);
    }
    GripTileMap map = tile.textures[index];
    return float2(dot(map.u.xy, imagePixel) + map.u.z, dot(map.v.xy, imagePixel) + map.v.z);
}
