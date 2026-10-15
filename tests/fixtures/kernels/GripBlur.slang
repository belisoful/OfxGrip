import GripKernel;

/* FxGripShader
{
    colorSpace: { transfer: "linear", primaries: "rec709" },
    parameters: [
        { id: 1, type: "float", name: "Radius", key: "radius", pixelUnits: true,
          default: 8, minimum: 0, maximum: 100 },
        { id: 2, type: "integer", name: "Taps", key: "TAPS", constant: true,
          default: 9, minimum: 1, maximum: 33 },
        { id: 3, type: "toggle", name: "Invert", key: "INVERT", constant: true, default: false },
        { id: 10, type: "group", name: "Tint", parameters: [
            { id: 11, type: "rgb", name: "Color", key: "tint",
              default: { red: 1, green: 1, blue: 1 } },
        ] },
        { id: 12, type: "imageref", name: "Matte", key: "matte" },
    ],
}
*/

struct BlurUniforms
{
    float radius;
    float3 tint;
    int2 offset;
    bool enabled;
};

ConstantBuffer<BlurUniforms> uniforms;
ConstantBuffer<GripTile> tile;
Texture2D<float4> source;
Texture2D<float4> matte;
RWTexture2D<float4> destination;
SamplerState bilinear;

[SpecializationConstant] const int TAPS = 9;
[SpecializationConstant] const bool INVERT = false;

[shader("compute")]
[numthreads(8, 8, 1)]
void fxGripKernel(uint3 tid : SV_DispatchThreadID)
{
    if (!gripInsideTile(tile, tid.xy))
    {
        return;
    }
    float2 imagePixel = gripImagePixel(tile, tid.xy);
    float2 uv = gripTexCoord(tile, 0, imagePixel);
    uint width, height;
    source.GetDimensions(width, height);
    float2 reach = float2(uniforms.radius / float(width), 0.0);
    float4 sum = 0;
    for (int tap = 0; tap < TAPS; tap++)
    {
        float position = TAPS > 1 ? float(tap) / float(TAPS - 1) * 2.0 - 1.0 : 0.0;
        sum += source.SampleLevel(bilinear, uv + reach * position, 0);
    }
    float4 color = sum / float(TAPS);
    float4 m = matte.SampleLevel(bilinear, gripTexCoord(tile, 1, imagePixel), 0);
    float3 rgb = color.rgb * uniforms.tint * m.a;
    if (INVERT)
    {
        rgb = color.a - rgb;
    }
    destination[tid.xy] = float4(rgb, color.a);
}
