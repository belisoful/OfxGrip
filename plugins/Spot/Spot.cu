#include "slang-cuda-prelude.h"


#line 28 "Spot.slang"
struct SpotUniforms_0
{
    float2  center_0;
    float radius_0;
    float softness_0;
    float outsideGain_0;
    float3  tint_0;
};


#line 29 "../../kernels/GripKernel.slang"
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


#line 43 "Spot.slang"
struct GlobalParams_0
{
    SpotUniforms_0* uniforms_0;
    GripTile_0* tile_0;
    CUtexObject source_0;
    CUsurfObject destination_0;
    SamplerState bilinear_0;
    bool INVERT_0;
};


#line 43
extern "C" __constant__ GlobalParams_0 SLANG_globalParams;
#define globalParams_0 (&SLANG_globalParams)

#line 9341 "hlsl.meta.slang"
static __device__ float clamp_0(float x_0, float minBound_0, float maxBound_0)
{

#line 9353
    return (F32_min(((F32_max((x_0), (minBound_0)))), (maxBound_0)));
}


#line 14946
static __device__ float saturate_0(float x_1)
{

#line 14954
    return clamp_0(x_1, 0.0f, 1.0f);
}


#line 12691
static __device__ float lerp_0(float x_2, float y_0, float s_0)
{

#line 12703
    return x_2 + (y_0 - x_2) * s_0;
}


#line 15362
static __device__ float smoothstep_0(float min_0, float max_0, float x_3)
{

#line 15374
    float _S1 = saturate_0((x_3 - min_0) / (max_0 - min_0));
    return _S1 * _S1 * (3.0f - (_S1 + _S1));
}


#line 10374
static __device__ float dot_0(float2  x_4, float2  y_1)
{

#line 10401
    return x_4.x * y_1.x + x_4.y * y_1.y;
}


#line 12651
static __device__ float length_0(float2  x_5)
{

#line 12663
    return (F32_sqrt((dot_0(x_5, x_5))));
}


#line 55 "Spot.slang"
static __device__ float3  slang_ldg_0(float3  * ptr_0)
{

#line 55
    float _S2 = __ldg(&(ptr_0->x));

#line 55
    float _S3 = __ldg(&(ptr_0->y));

#line 55
    float _S4 = __ldg(&(ptr_0->z));

#line 55
    return make_float3 (_S2, _S3, _S4);
}


#line 3785 "hlsl.meta.slang"
static __device__ GripTileMap_0 slang_ldg_1(GripTileMap_0 * ptr_1)
{

#line 3785
    float4  _S5 = __ldg(&ptr_1->u_0);

#line 3785
    float4  _S6 = __ldg(&ptr_1->v_0);

#line 3785
    GripTileMap_0 _S7 = { _S5, _S6 };

#line 3785
    return _S7;
}


#line 2840 "core.meta.slang"
static __device__ uint2  gripTileSize_0()
{

#line 2840
    float4  _S8 = __ldg(&globalParams_0->tile_0->destinationBounds_0);

#line 54 "../../kernels/GripKernel.slang"
    return make_uint2 (uint(_S8.z - _S8.x), uint(_S8.w - _S8.y));
}


#line 54
static __device__ bool gripInsideTile_0(uint2  _S9)
{

#line 54
    uint2  _S10 = gripTileSize_0();

#line 54
    bool _S11;

#line 62
    if((_S9.x) < (_S10.x))
    {

#line 62
        _S11 = (_S9.y) < (_S10.y);

#line 62
    }
    else
    {

#line 62
        _S11 = false;

#line 62
    }

#line 62
    return _S11;
}


#line 62
static __device__ float2  gripImagePixel_0(uint2  _S12)
{

#line 62
    float4  _S13 = __ldg(&globalParams_0->tile_0->destinationBounds_0);

#line 62
    uint _S14 = __ldg(&globalParams_0->tile_0->reserved_0);

#line 70
    if((_S14 & 1U) != 0U)
    {
        return make_float2 (_S13.x + float(_S12.x) + 0.5f, _S13.y + float(_S12.y) + 0.5f);
    }
    return make_float2 (_S13.x + float(_S12.x) + 0.5f, _S13.w - float(_S12.y) - 0.5f);
}


#line 484 "core"
static __device__ float2  gripTexCoord_0(uint _S15, float2  _S16)
{

#line 81 "../../kernels/GripKernel.slang"
    if(_S15 >= 16U)
    {
        return make_float2 (0.0f);
    }

#line 83
    GripTileMap_0 _S17 = slang_ldg_1(&globalParams_0->tile_0->textures_0[_S15]);

#line 83
    float4  _S18 = _S17.u_0;

#line 83
    float4  _S19 = _S17.v_0;


    return make_float2 (dot_0(float2 {_S18.x, _S18.y}, _S16) + _S17.u_0.z, dot_0(float2 {_S19.x, _S19.y}, _S16) + _S17.v_0.z);
}


#line 47 "Spot.slang"
extern "C" __global__ void spotKernel()
{

#line 47
    uint3  _S20 = blockIdx * blockDim + threadIdx;

    uint2  _S21 = uint2 {_S20.x, _S20.y};

#line 49
    bool _S22 = gripInsideTile_0(_S21);

#line 49
    if(!_S22)
    {
        return;
    }

#line 51
    float2  _S23 = gripImagePixel_0(_S21);

#line 51
    float2  _S24 = gripTexCoord_0(0U, _S23);


    float4  color_0 = (tex2DLod<float4 >((globalParams_0->source_0), ((_S24)).x, ((_S24)).y, ((0.0f))));

#line 54
    float2  _S25 = __ldg(&globalParams_0->uniforms_0->center_0);
    float distance_0 = length_0(_S23 - _S25);

#line 55
    float _S26 = __ldg(&globalParams_0->uniforms_0->radius_0);

#line 55
    float _S27 = __ldg(&globalParams_0->uniforms_0->radius_0);

#line 55
    float _S28 = __ldg(&globalParams_0->uniforms_0->softness_0);
    float outside_0 = smoothstep_0(_S26, _S27 + (F32_max((_S28), (0.00009999999747379f))), distance_0);

#line 56
    float outside_1;
    if(globalParams_0->INVERT_0)
    {

#line 57
        outside_1 = 1.0f - outside_0;

#line 57
    }
    else
    {

#line 57
        outside_1 = outside_0;

#line 57
    }

#line 57
    float _S29 = __ldg(&globalParams_0->uniforms_0->outsideGain_0);

#line 62
    float3  _S30 = float3 {color_0.x, color_0.y, color_0.z} * make_float3 (lerp_0(1.0f, _S29, outside_1));

#line 62
    float3  _S31 = slang_ldg_0(&globalParams_0->uniforms_0->tint_0);

#line 62
    surf2Dwrite<float4 >((make_float4 ((_S30 * _S31).x, (_S30 * _S31).y, (_S30 * _S31).z, color_0.w)), (globalParams_0->destination_0), ((_S21)).x * 16, ((_S21)).y, SLANG_CUDA_BOUNDARY_MODE);
    return;
}

