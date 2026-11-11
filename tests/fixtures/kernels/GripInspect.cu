#include "slang-cuda-prelude.h"


#line 24 "GripInspect.slang"
struct InspectValues_0
{
    float radius_0;
    float gain_0;
    float2  center_0;
    bool invert_0;
    uint count_0;
    __half level_0;
    short small_0;
    uchar byte_0;
    float unbound_0;
};


#line 29 "../../../kernels/GripKernel.slang"
struct GripTileMap_0
{
    float4  u_0;
    float4  v_0;
};


#line 41
struct GripTile_0
{
    float4  destinationBounds_0;
    float2  pixelScale_0;
    uint textureCount_0;
    uint reserved_0;
    FixedArray<GripTileMap_0, 16>  textures_0;
};


#line 48 "GripInspect.slang"
struct GlobalParams_0
{
    InspectValues_0* values_0;
    GripTile_0* tile_0;
    CUtexObject first_0;
    CUtexObject second_0;
    CUtexObject ramp_0;
    CUtexObject mask_0;
    CUsurfObject destination_0;
    SamplerState bilinear_0;
    int MODE_0;
    int TAPS_0;
    float FILL_0;
};


#line 48
extern "C" __constant__ GlobalParams_0 SLANG_globalParams;
#define globalParams_0 (&SLANG_globalParams)

#line 10374 "hlsl.meta.slang"
static __device__ float dot_0(float2  x_0, float2  y_0)
{

#line 10401
    return x_0.x * y_0.x + x_0.y * y_0.y;
}


#line 10401
static __device__ GripTileMap_0 slang_ldg_0(GripTileMap_0 * ptr_0)
{

#line 10401
    float4  _S1 = __ldg(&ptr_0->u_0);

#line 10401
    float4  _S2 = __ldg(&ptr_0->v_0);

#line 10401
    GripTileMap_0 _S3 = { _S1, _S2 };

#line 10401
    return _S3;
}


#line 2840 "core.meta.slang"
static __device__ uint2  gripTileSize_0()
{

#line 2840
    float4  _S4 = __ldg(&globalParams_0->tile_0->destinationBounds_0);

#line 54 "../../../kernels/GripKernel.slang"
    return make_uint2 (uint(_S4.z - _S4.x), uint(_S4.w - _S4.y));
}


#line 54
static __device__ bool gripInsideTile_0(uint2  _S5)
{

#line 54
    uint2  _S6 = gripTileSize_0();

#line 54
    bool _S7;

#line 62
    if((_S5.x) < (_S6.x))
    {

#line 62
        _S7 = (_S5.y) < (_S6.y);

#line 62
    }
    else
    {

#line 62
        _S7 = false;

#line 62
    }

#line 62
    return _S7;
}


#line 62
static __device__ float2  gripImagePixel_0(uint2  _S8)
{

#line 62
    float4  _S9 = __ldg(&globalParams_0->tile_0->destinationBounds_0);

#line 62
    uint _S10 = __ldg(&globalParams_0->tile_0->reserved_0);

#line 70
    if((_S10 & 1U) != 0U)
    {
        return make_float2 (_S9.x + float(_S8.x) + 0.5f, _S9.y + float(_S8.y) + 0.5f);
    }
    return make_float2 (_S9.x + float(_S8.x) + 0.5f, _S9.w - float(_S8.y) - 0.5f);
}


#line 74
static __device__ float2  gripTexCoord_0(uint _S11, float2  _S12)
{

#line 81
    if(_S11 >= 16U)
    {
        return make_float2 (0.0f);
    }

#line 83
    GripTileMap_0 _S13 = slang_ldg_0(&globalParams_0->tile_0->textures_0[_S11]);

#line 83
    float4  _S14 = _S13.u_0;

#line 83
    float4  _S15 = _S13.v_0;


    return make_float2 (dot_0(float2 {_S14.x, _S14.y}, _S12) + _S13.u_0.z, dot_0(float2 {_S15.x, _S15.y}, _S12) + _S13.v_0.z);
}


#line 54 "GripInspect.slang"
extern "C" __global__ void fxGripKernel()
{

#line 54
    uint3  _S16 = blockIdx * blockDim + threadIdx;

    uint2  _S17 = uint2 {_S16.x, _S16.y};

#line 56
    bool _S18 = gripInsideTile_0(_S17);

#line 56
    if(!_S18)
    {
        return;
    }

#line 58
    float2  _S19 = gripImagePixel_0(_S17);

#line 58
    float4  result_0;



    switch(globalParams_0->MODE_0)
    {
    case int(1):
        {

#line 62
            float2  _S20 = __ldg(&globalParams_0->tile_0->pixelScale_0);

#line 62
            result_0 = make_float4 (_S19.x, _S19.y, _S20.x, _S20.y);



            break;
        }
    case int(2):
        {

#line 66
            float2  _S21 = gripTexCoord_0(0U, _S19);

#line 66
            float2  _S22 = gripTexCoord_0(1U, _S19);

#line 66
            result_0 = make_float4 (_S21.x, _S21.y, _S22.x, _S22.y);


            break;
        }
    case int(3):
        {

#line 69
            float2  _S23 = gripTexCoord_0(0U, _S19);

            float4  _S24 = (tex2DLod<float4 >((globalParams_0->first_0), ((_S23)).x, ((_S23)).y, ((0.0f))));

#line 71
            float2  _S25 = gripTexCoord_0(1U, _S19);

#line 71
            result_0 = _S24 - (tex2DLod<float4 >((globalParams_0->second_0), ((_S25)).x, ((_S25)).y, ((0.0f))));

            break;
        }
    case int(4):
        {

#line 73
            float2  _S26 = __ldg(&globalParams_0->values_0->center_0);

#line 73
            bool _S27 = __ldg(&globalParams_0->values_0->invert_0);

            float _S28 = float(_S27);

#line 75
            uint _S29 = __ldg(&globalParams_0->values_0->count_0);

#line 75
            result_0 = make_float4 (_S26.x, _S26.y, _S28, float(_S29));
            break;
        }
    case int(5):
        {

#line 76
            __half _S30 = __ldg(&globalParams_0->values_0->level_0);

            float _S31 = float(_S30);

#line 78
            short _S32 = __ldg(&globalParams_0->values_0->small_0);

#line 78
            float _S33 = float(_S32);

#line 78
            uchar _S34 = __ldg(&globalParams_0->values_0->byte_0);

#line 78
            float _S35 = float(_S34);

#line 78
            float _S36 = __ldg(&globalParams_0->values_0->unbound_0);

#line 78
            result_0 = make_float4 (_S31, _S33, _S35, _S36);
            break;
        }
    case int(6):
        {

#line 79
            float2  _S37 = gripTexCoord_0(3U, _S19);

            float4  _S38 = (tex2DLod<float4 >((globalParams_0->mask_0), ((_S37)).x, ((_S37)).y, ((0.0f))));

#line 81
            float2  _S39 = float2 {_S38.x, _S38.y};
            int3  _S40 = make_int3 (int(2), int(0), int(0));

#line 82
            float _S41 = (tex2Dfetch_int<float4 >((globalParams_0->ramp_0), ((_S40)).x, ((_S40)).y, ((_S40)).z)).z;

#line 82
            uint _S42 = __ldg(&globalParams_0->tile_0->textureCount_0);

#line 82
            result_0 = make_float4 (_S39.x, _S39.y, _S41, float(_S42));
            break;
        }
    case int(7):
        {

#line 83
            float2  _S43 = gripTexCoord_0(0U, _S19);


            float4  color_0 = (tex2DLod<float4 >((globalParams_0->first_0), ((_S43)).x, ((_S43)).y, ((0.0f))));

#line 86
            result_0 = make_float4 (color_0.x, 0.0f, 0.0f, color_0.w);

            break;
        }
    case int(8):
        {

#line 88
            result_0 = make_float4 (globalParams_0->FILL_0);



            break;
        }
    default:
        {

#line 92
            float _S44 = __ldg(&globalParams_0->values_0->radius_0);

#line 92
            float _S45 = __ldg(&globalParams_0->values_0->gain_0);

#line 92
            result_0 = make_float4 (_S44, _S45, float(globalParams_0->TAPS_0), 1.0f);


            break;
        }
    }

#line 97
    surf2Dwrite<float4 >((result_0), (globalParams_0->destination_0), ((_S17)).x * 16, ((_S17)).y, SLANG_CUDA_BOUNDARY_MODE);
    return;
}

