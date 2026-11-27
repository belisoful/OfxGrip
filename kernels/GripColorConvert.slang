// GripColorConvert.slang
// Copyright © 2026 Belisoful. MIT License.
//
// The colourspace conversion the kernel engine runs around a kernel whose colorSpace differs from
// the host's. One pass decodes each texel to linear light, applies a 3×3 gamut matrix, and
// encodes. The engine binds a source whose bounds equal the destination window, so every sample
// lands on a texel center. The transfer numbers are the values of ColorTransfer in
// core/ColorGamut.hpp, and the curves are the ones in core/ColorGamut.cpp.
import GripKernel;

/* FxGripShader
{
    parameters: [
        { id: 1, type: "rgb", name: "Row 0", key: "row0" },
        { id: 2, type: "rgb", name: "Row 1", key: "row1" },
        { id: 3, type: "rgb", name: "Row 2", key: "row2" },
        { id: 4, type: "integer", name: "Decode", key: "decode" },
        { id: 5, type: "integer", name: "Encode", key: "encode" },
        { id: 6, type: "integer", name: "Premultiplied", key: "premultiplied" },
    ],
}
*/

struct GripColorUniforms
{
    float3 row0;
    float3 row1;
    float3 row2;
    int decode;
    int encode;
    int premultiplied;
};

ConstantBuffer<GripColorUniforms> uniforms;
ConstantBuffer<GripTile> tile;
Texture2D<float4> source;
RWTexture2D<float4> destination;
SamplerState bilinear;

static const float kPqM1 = 0.1593017578125;
static const float kPqM2 = 78.84375;
static const float kPqC1 = 0.8359375;
static const float kPqC2 = 18.8515625;
static const float kPqC3 = 18.6875;
static const float kHlgA = 0.17883277;
static const float kHlgB = 0.28466892;
static const float kHlgC = 0.55991073;

/// A power of a non-negative base. A library pow carries every special case of the function and
/// is several times the size of this on a GPU target, and the kernel calls it per channel.
float gripPow(float base, float exponent)
{
    return base > 0.0 ? exp(exponent * log(base)) : 0.0;
}

/// A piecewise power curve: a linear segment up to `threshold`, a power above it.
/// - Decode: e <= threshold ? e * slope : pow((e + offset) * scale, exponent).
/// - Encode: l <= threshold ? l * slope : scale * pow(l, exponent) - offset.
/// A pure power has a threshold below zero.
struct GripCurve
{
    float threshold;
    float slope;
    float offset;
    float scale;
    float exponent;
};

GripCurve gripDecodeCurve(int transfer)
{
    GripCurve curve;
    curve.threshold = -1.0;
    curve.slope = 1.0;
    curve.offset = 0.0;
    curve.scale = 1.0;
    curve.exponent = 1.0;
    switch (transfer)
    {
    case 1: curve.threshold = 0.04045; curve.slope = 1.0 / 12.92; curve.offset = 0.055; curve.scale = 1.0 / 1.055; curve.exponent = 2.4; break;
    case 2: curve.threshold = 0.081; curve.slope = 1.0 / 4.5; curve.offset = 0.099; curve.scale = 1.0 / 1.099; curve.exponent = 1.0 / 0.45; break;
    case 3: curve.exponent = 1.8; break;
    case 4: curve.exponent = 2.2; break;
    case 5: curve.exponent = 2.4; break;
    case 6: curve.exponent = 2.6; break;
    default: break;
    }
    return curve;
}

GripCurve gripEncodeCurve(int transfer)
{
    GripCurve curve;
    curve.threshold = -1.0;
    curve.slope = 1.0;
    curve.offset = 0.0;
    curve.scale = 1.0;
    curve.exponent = 1.0;
    switch (transfer)
    {
    case 1: curve.threshold = 0.0031308; curve.slope = 12.92; curve.offset = 0.055; curve.scale = 1.055; curve.exponent = 1.0 / 2.4; break;
    case 2: curve.threshold = 0.018; curve.slope = 4.5; curve.offset = 0.099; curve.scale = 1.099; curve.exponent = 0.45; break;
    case 3: curve.exponent = 1.0 / 1.8; break;
    case 4: curve.exponent = 1.0 / 2.2; break;
    case 5: curve.exponent = 1.0 / 2.4; break;
    case 6: curve.exponent = 1.0 / 2.6; break;
    default: break;
    }
    return curve;
}

float gripDecode(int transfer, GripCurve curve, float encoded)
{
    if (transfer == 0)
    {
        return encoded;
    }
    if (transfer == 7)
    {
        float p = gripPow(clamp(encoded, 0.0, 1.0), 1.0 / kPqM2);
        return gripPow(max(p - kPqC1, 0.0) / (kPqC2 - kPqC3 * p), 1.0 / kPqM1);
    }
    if (transfer == 8)
    {
        float e = clamp(encoded, 0.0, 1.0);
        return e <= 0.5 ? e * e / 3.0 : (exp((e - kHlgC) / kHlgA) + kHlgB) / 12.0;
    }
    float magnitude = abs(encoded);
    float linear = magnitude <= curve.threshold ? magnitude * curve.slope : gripPow((magnitude + curve.offset) * curve.scale, curve.exponent);
    return encoded < 0.0 ? -linear : linear;
}

float gripEncode(int transfer, GripCurve curve, float linear)
{
    if (transfer == 0)
    {
        return linear;
    }
    if (transfer == 7)
    {
        float p = gripPow(clamp(linear, 0.0, 1.0), kPqM1);
        return gripPow((kPqC1 + kPqC2 * p) / (1.0 + kPqC3 * p), kPqM2);
    }
    if (transfer == 8)
    {
        float l = clamp(linear, 0.0, 1.0);
        return l <= 1.0 / 12.0 ? sqrt(3.0 * l) : kHlgA * log(12.0 * l - kHlgB) + kHlgC;
    }
    float magnitude = abs(linear);
    float encoded = magnitude <= curve.threshold ? magnitude * curve.slope : curve.scale * gripPow(magnitude, curve.exponent) - curve.offset;
    return linear < 0.0 ? -encoded : encoded;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void gripColorConvert(uint3 tid : SV_DispatchThreadID)
{
    if (!gripInsideTile(tile, tid.xy))
    {
        return;
    }
    float2 imagePixel = gripImagePixel(tile, tid.xy);
    float4 color = source.SampleLevel(bilinear, gripTexCoord(tile, 0, imagePixel), 0);
    // A transfer curve applies to unpremultiplied components. The matrix alone commutes with
    // the alpha product, so a matrix-only pass leaves the components as they are.
    bool curved = uniforms.decode != 0 || uniforms.encode != 0;
    bool unpremultiply = curved && uniforms.premultiplied != 0 && color.a > 0.0;
    float3 rgb = unpremultiply ? color.rgb / color.a : color.rgb;
    GripCurve decode = gripDecodeCurve(uniforms.decode);
    GripCurve encode = gripEncodeCurve(uniforms.encode);
    rgb = float3(gripDecode(uniforms.decode, decode, rgb.r), gripDecode(uniforms.decode, decode, rgb.g), gripDecode(uniforms.decode, decode, rgb.b));
    rgb = float3(dot(uniforms.row0, rgb), dot(uniforms.row1, rgb), dot(uniforms.row2, rgb));
    rgb = float3(gripEncode(uniforms.encode, encode, rgb.r), gripEncode(uniforms.encode, encode, rgb.g), gripEncode(uniforms.encode, encode, rgb.b));
    if (unpremultiply)
    {
        rgb *= color.a;
    }
    destination[tid.xy] = float4(rgb, color.a);
}
