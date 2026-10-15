#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 46 "GripInspect.slang"
constant int fc_MODE_0 [[function_constant(0)]];
constant int MODE_0 = is_function_constant_defined(fc_MODE_0) ? fc_MODE_0 : int(0);
constant float fc_FILL_0 [[function_constant(2)]];
constant float FILL_0 = is_function_constant_defined(fc_FILL_0) ? fc_FILL_0 : 0.0f;

#line 47
constant int fc_TAPS_0 [[function_constant(1)]];
constant int TAPS_0 = is_function_constant_defined(fc_TAPS_0) ? fc_TAPS_0 : int(1);

#line 29 "../../../kernels/GripKernel.slang"
struct GripTileMap_0
{
    float4 u_0;
    float4 v_0;
};


#line 41
struct GripTile_0
{
    float4 destinationBounds_0;
    float2 pixelScale_0;
    uint textureCount_0;
    uint reserved_0;
    array<GripTileMap_0, int(16)> textures_0;
};


#line 24 "GripInspect.slang"
struct InspectValues_0
{
    float radius_0;
    float gain_0;
    float2 center_0;
    bool invert_0;
    uint count_0;
    half level_0;
    short small_0;
    uchar byte_0;
    float unbound_0;
};


#line 43
struct KernelContext_0
{
    GripTile_0 constant* tile_0;
    texture2d<float, access::sample> first_0;
    sampler bilinear_0;
    texture2d<float, access::sample> second_0;
    InspectValues_0 constant* values_0;
    texture2d<float, access::sample> mask_0;
    texture2d<float, access::sample> ramp_0;
    texture2d<float, access::read_write> destination_0;
};


#line 2839 "core.meta.slang"
uint2 gripTileSize_0(KernelContext_0 thread* kernelContext_0)
{

#line 54 "../../../kernels/GripKernel.slang"
    return uint2(uint(kernelContext_0->tile_0->destinationBounds_0.z - kernelContext_0->tile_0->destinationBounds_0.x), uint(kernelContext_0->tile_0->destinationBounds_0.w - kernelContext_0->tile_0->destinationBounds_0.y));
}


#line 54
bool gripInsideTile_0(uint2 _S1, KernelContext_0 thread* kernelContext_1)
{

#line 54
    uint2 _S2 = gripTileSize_0(kernelContext_1);

#line 54
    bool _S3;

#line 62
    if((_S1.x) < (_S2.x))
    {

#line 62
        _S3 = (_S1.y) < (_S2.y);

#line 62
    }
    else
    {

#line 62
        _S3 = false;

#line 62
    }

#line 62
    return _S3;
}


#line 62
float2 gripImagePixel_0(uint2 _S4, KernelContext_0 thread* kernelContext_2)
{

#line 62
    float4 d_0 = kernelContext_2->tile_0->destinationBounds_0;

#line 70
    if(((kernelContext_2->tile_0->reserved_0) & 1U) != 0U)
    {
        return float2(d_0.x + float(_S4.x) + 0.5f, d_0.y + float(_S4.y) + 0.5f);
    }
    return float2(d_0.x + float(_S4.x) + 0.5f, d_0.w - float(_S4.y) - 0.5f);
}


#line 74
float2 gripTexCoord_0(uint _S5, float2 _S6, KernelContext_0 thread* kernelContext_3)
{

#line 81
    if(_S5 >= 16U)
    {
        return float2(0.0f) ;
    }

    return float2(dot(kernelContext_3->tile_0->textures_0[_S5].u_0.xy, _S6) + kernelContext_3->tile_0->textures_0[_S5].u_0.z, dot(kernelContext_3->tile_0->textures_0[_S5].v_0.xy, _S6) + kernelContext_3->tile_0->textures_0[_S5].v_0.z);
}


#line 54 "GripInspect.slang"
[[kernel]] void fxGripKernel(uint3 tid_0 [[thread_position_in_grid]], GripTile_0 constant* tile_1 [[buffer(1)]], texture2d<float, access::sample> first_1 [[texture(0)]], sampler bilinear_1 [[sampler(0)]], texture2d<float, access::sample> second_1 [[texture(1)]], InspectValues_0 constant* values_1 [[buffer(0)]], texture2d<float, access::sample> mask_1 [[texture(3)]], texture2d<float, access::sample> ramp_1 [[texture(2)]], texture2d<float, access::read_write> destination_1 [[texture(4)]])
{

#line 54
    thread KernelContext_0 kernelContext_4;

#line 54
    (&kernelContext_4)->tile_0 = tile_1;

#line 54
    (&kernelContext_4)->first_0 = first_1;

#line 54
    (&kernelContext_4)->bilinear_0 = bilinear_1;

#line 54
    (&kernelContext_4)->second_0 = second_1;

#line 54
    (&kernelContext_4)->values_0 = values_1;

#line 54
    (&kernelContext_4)->mask_0 = mask_1;

#line 54
    (&kernelContext_4)->ramp_0 = ramp_1;

#line 54
    (&kernelContext_4)->destination_0 = destination_1;

    uint2 _S7 = tid_0.xy;

#line 56
    bool _S8 = gripInsideTile_0(_S7, &kernelContext_4);

#line 56
    if(!_S8)
    {
        return;
    }

#line 58
    float2 _S9 = gripImagePixel_0(_S7, &kernelContext_4);

#line 58
    float4 result_0;



    switch(MODE_0)
    {
    case int(1):
        {

#line 62
            result_0 = float4(_S9, (&kernelContext_4)->tile_0->pixelScale_0);



            break;
        }
    case int(2):
        {

#line 66
            float2 _S10 = gripTexCoord_0(0U, _S9, &kernelContext_4);

#line 66
            float2 _S11 = gripTexCoord_0(1U, _S9, &kernelContext_4);

#line 66
            result_0 = float4(_S10, _S11);


            break;
        }
    case int(3):
        {

#line 69
            float2 _S12 = gripTexCoord_0(0U, _S9, &kernelContext_4);

            float4 _S13 = (((&kernelContext_4)->first_0).sample(((&kernelContext_4)->bilinear_0), (_S12), level((0.0f))));

#line 71
            float2 _S14 = gripTexCoord_0(1U, _S9, &kernelContext_4);

#line 71
            result_0 = _S13 - (((&kernelContext_4)->second_0).sample(((&kernelContext_4)->bilinear_0), (_S14), level((0.0f))));

            break;
        }
    case int(4):
        {

#line 73
            result_0 = float4((&kernelContext_4)->values_0->center_0, float((&kernelContext_4)->values_0->invert_0), float((&kernelContext_4)->values_0->count_0));


            break;
        }
    case int(5):
        {

#line 76
            result_0 = float4(float((&kernelContext_4)->values_0->level_0), float((&kernelContext_4)->values_0->small_0), float((&kernelContext_4)->values_0->byte_0), (&kernelContext_4)->values_0->unbound_0);


            break;
        }
    case int(6):
        {

#line 79
            float2 _S15 = gripTexCoord_0(3U, _S9, &kernelContext_4);


            int3 _S16 = int3(int(2), int(0), int(0));

#line 82
            result_0 = float4((((&kernelContext_4)->mask_0).sample(((&kernelContext_4)->bilinear_0), (_S15), level((0.0f)))).xy, (((&kernelContext_4)->ramp_0).read(vec<uint,2>(((_S16)).xy), uint(((_S16)).z))).z, float((&kernelContext_4)->tile_0->textureCount_0));
            break;
        }
    case int(7):
        {

#line 83
            float2 _S17 = gripTexCoord_0(0U, _S9, &kernelContext_4);


            float4 color_0 = (((&kernelContext_4)->first_0).sample(((&kernelContext_4)->bilinear_0), (_S17), level((0.0f))));

#line 86
            result_0 = float4(color_0.x, 0.0f, 0.0f, color_0.w);

            break;
        }
    case int(8):
        {

#line 88
            result_0 = float4(FILL_0) ;



            break;
        }
    default:
        {

#line 92
            result_0 = float4((&kernelContext_4)->values_0->radius_0, (&kernelContext_4)->values_0->gain_0, float(TAPS_0), 1.0f);


            break;
        }
    }

#line 97
    (&kernelContext_4)->destination_0.write(result_0,_S7);
    return;
}

