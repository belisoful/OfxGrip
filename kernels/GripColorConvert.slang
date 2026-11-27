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

float gripDecodeMagnitude(int transfer, float e)
{
    switch (transfer)
    {
    case 1: return e <= 0.04045 ? e / 12.92 : pow((e + 0.055) / 1.055, 2.4);
    case 2: return e < 0.081 ? e / 4.5 : pow((e + 0.099) / 1.099, 1.0 / 0.45);
    case 3: return pow(e, 1.8);
    case 4: return pow(e, 2.2);
    case 5: return pow(e, 2.4);
    case 6: return pow(e, 2.6);
    default: return e;
    }
}

float gripDecode(int transfer, float encoded)
{
    if (transfer == 0)
    {
        return encoded;
    }
    if (transfer == 7)
    {
        float p = pow(clamp(encoded, 0.0, 1.0), 1.0 / kPqM2);
        return pow(max(p - kPqC1, 0.0) / (kPqC2 - kPqC3 * p), 1.0 / kPqM1);
    }
    if (transfer == 8)
    {
        float e = clamp(encoded, 0.0, 1.0);
        return e <= 0.5 ? e * e / 3.0 : (exp((e - kHlgC) / kHlgA) + kHlgB) / 12.0;
    }
    return encoded < 0.0 ? -gripDecodeMagnitude(transfer, -encoded) : gripDecodeMagnitude(transfer, encoded);
}

float gripEncodeMagnitude(int transfer, float l)
{
    switch (transfer)
    {
    case 1: return l <= 0.0031308 ? l * 12.92 : 1.055 * pow(l, 1.0 / 2.4) - 0.055;
    case 2: return l < 0.018 ? l * 4.5 : 1.099 * pow(l, 0.45) - 0.099;
    case 3: return pow(l, 1.0 / 1.8);
    case 4: return pow(l, 1.0 / 2.2);
    case 5: return pow(l, 1.0 / 2.4);
    case 6: return pow(l, 1.0 / 2.6);
    default: return l;
    }
}

float gripEncode(int transfer, float linear)
{
    if (transfer == 0)
    {
        return linear;
    }
    if (transfer == 7)
    {
        float p = pow(clamp(linear, 0.0, 1.0), kPqM1);
        return pow((kPqC1 + kPqC2 * p) / (1.0 + kPqC3 * p), kPqM2);
    }
    if (transfer == 8)
    {
        float l = clamp(linear, 0.0, 1.0);
        return l <= 1.0 / 12.0 ? sqrt(3.0 * l) : kHlgA * log(12.0 * l - kHlgB) + kHlgC;
    }
    return linear < 0.0 ? -gripEncodeMagnitude(transfer, -linear) : gripEncodeMagnitude(transfer, linear);
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
    rgb = float3(gripDecode(uniforms.decode, rgb.r), gripDecode(uniforms.decode, rgb.g), gripDecode(uniforms.decode, rgb.b));
    rgb = float3(dot(uniforms.row0, rgb), dot(uniforms.row1, rgb), dot(uniforms.row2, rgb));
    rgb = float3(gripEncode(uniforms.encode, rgb.r), gripEncode(uniforms.encode, rgb.g), gripEncode(uniforms.encode, rgb.b));
    if (unpremultiply)
    {
        rgb *= color.a;
    }
    destination[tid.xy] = float4(rgb, color.a);
}
