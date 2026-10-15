// Spot.slang
// Copyright © 2026 Belisoful. MIT License.
//
// The OfxGrip kernel sample: a soft spotlight that keeps the image inside a circle and dims it
// outside. One Slang compute kernel against the GripKernel module renders on Metal and on the CPU.
import GripKernel;

/* FxGripShader
{
    colorSpace: { transfer: "linear", primaries: "host" },
    parameters: [
        { id: 1, type: "point", name: "Center", key: "center", pixelUnits: true,
          default: { x: 960, y: 540 } },
        { id: 2, type: "float", name: "Radius", key: "radius", pixelUnits: true,
          default: 300, minimum: 0, maximum: 8000, slidermax: 2000 },
        { id: 3, type: "float", name: "Softness", key: "softness", pixelUnits: true,
          default: 150, minimum: 0, maximum: 8000, slidermax: 1000 },
        { id: 4, type: "float", name: "Outside Gain", key: "outsideGain",
          default: 0.25, minimum: 0, maximum: 4, slidermax: 1 },
        { id: 10, type: "group", name: "Look", parameters: [
            { id: 11, type: "rgb", name: "Tint", key: "tint", default: { red: 1, green: 1, blue: 1 } },
            { id: 12, type: "toggle", name: "Invert", key: "INVERT", constant: true, default: false },
        ] },
    ],
}
*/

struct SpotUniforms
{
    float2 center;
    float radius;
    float softness;
    float outsideGain;
    float3 tint;
};

ConstantBuffer<SpotUniforms> uniforms;
ConstantBuffer<GripTile> tile;
Texture2D<float4> source;
RWTexture2D<float4> destination;
SamplerState bilinear;

[SpecializationConstant] const bool INVERT = false;

[shader("compute")]
[numthreads(8, 8, 1)]
void spotKernel(uint3 tid : SV_DispatchThreadID)
{
    if (!gripInsideTile(tile, tid.xy))
    {
        return;
    }
    float2 imagePixel = gripImagePixel(tile, tid.xy);
    float4 color = source.SampleLevel(bilinear, gripTexCoord(tile, 0, imagePixel), 0);
    float distance = length(imagePixel - uniforms.center);
    float outside = smoothstep(uniforms.radius, uniforms.radius + max(uniforms.softness, 0.0001), distance);
    if (INVERT)
    {
        outside = 1.0 - outside;
    }
    float gain = lerp(1.0, uniforms.outsideGain, outside);
    destination[tid.xy] = float4(color.rgb * gain * uniforms.tint, color.a);
}
