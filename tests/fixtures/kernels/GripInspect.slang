import GripKernel;

/* FxGripShader
{
    colorSpace: { transfer: "linear", primaries: "rec2020" },
    parameters: [
        { id: 1, type: "float", name: "Radius", key: "radius", pixelUnits: true, default: 8 },
        { id: 2, type: "float", name: "Gain", key: "gain", default: 1 },
        { id: 3, type: "point", name: "Center", key: "center" },
        { id: 4, type: "toggle", name: "Invert", key: "invert" },
        { id: 5, type: "integer", name: "Count", key: "count" },
        { id: 6, type: "float", name: "Level", key: "level" },
        { id: 7, type: "integer", name: "Small", key: "small" },
        { id: 8, type: "integer", name: "Byte", key: "byte" },
        { id: 9, type: "integer", name: "Mode", key: "MODE", constant: true, default: 0 },
        { id: 10, type: "integer", name: "Taps", key: "TAPS", constant: true, pixelUnits: true, default: 1 },
        { id: 11, type: "float", name: "Fill", key: "FILL", constant: true, allowContinuousConstant: true },
        { id: 12, type: "gradient", name: "Ramp", key: "ramp" },
        { id: 13, type: "imageref", name: "Mask", key: "mask" },
    ],
}
*/

struct InspectValues
{
    float radius;
    float gain;
    float2 center;
    bool invert;
    uint count;
    half level;
    int16_t small;
    uint8_t byte;
    float unbound;
};

ConstantBuffer<InspectValues> values;
ConstantBuffer<GripTile> tile;
Texture2D<float4> first;
Texture2D<float4> second;
Texture2D<float4> ramp;
Texture2D<float4> mask;
RWTexture2D<float4> destination;
SamplerState bilinear;

[SpecializationConstant] const int MODE = 0;
[SpecializationConstant] const int TAPS = 1;
[SpecializationConstant] const float FILL = 0.0;

// Writes one of the bound values into every destination texel, selected by MODE, so a test reads
// the binding back as pixels.
[shader("compute")]
[numthreads(8, 8, 1)]
void fxGripKernel(uint3 tid : SV_DispatchThreadID)
{
    if (!gripInsideTile(tile, tid.xy))
    {
        return;
    }
    float2 imagePixel = gripImagePixel(tile, tid.xy);
    float4 result;
    switch (MODE)
    {
    case 1:
        result = float4(imagePixel, tile.pixelScale);
        break;
    case 2:
        result = float4(gripTexCoord(tile, 0, imagePixel), gripTexCoord(tile, 1, imagePixel));
        break;
    case 3:
        result = first.SampleLevel(bilinear, gripTexCoord(tile, 0, imagePixel), 0)
               - second.SampleLevel(bilinear, gripTexCoord(tile, 1, imagePixel), 0);
        break;
    case 4:
        result = float4(values.center, float(values.invert), float(values.count));
        break;
    case 5:
        result = float4(float(values.level), float(values.small), float(values.byte), values.unbound);
        break;
    case 6:
        result = float4(mask.SampleLevel(bilinear, gripTexCoord(tile, 3, imagePixel), 0).rg,
                        ramp.Load(int3(2, 0, 0)).b, float(tile.textureCount));
        break;
    case 7:
    {
        float4 color = first.SampleLevel(bilinear, gripTexCoord(tile, 0, imagePixel), 0);
        result = float4(color.r, 0.0, 0.0, color.a);
        break;
    }
    case 8:
        result = float4(FILL);
        break;
    default:
        result = float4(values.radius, values.gain, float(TAPS), 1.0);
        break;
    }
    destination[tid.xy] = result;
}
