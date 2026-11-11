#include "slang-cuda-prelude.h"


#line 21 "GripBlur.slang"
struct BlurUniforms_0
{
    float radius_0;
    float3  tint_0;
    int2  offset_0;
    bool enabled_0;
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


#line 55 "GripBlur.slang"
struct GlobalParams_0
{
    BlurUniforms_0* uniforms_0;
    GripTile_0* tile_0;
    CUtexObject source_0;
    CUtexObject matte_0;
    CUsurfObject destination_0;
    SamplerState bilinear_0;
    int TAPS_0;
    bool INVERT_0;
};


#line 55
extern "C" __constant__ GlobalParams_0 SLANG_globalParams;
#define globalParams_0 (&SLANG_globalParams)

#line 51
static __device__ float3  slang_ldg_0(float3  * ptr_0)
{

#line 51
    float _S1 = __ldg(&(ptr_0->x));

#line 51
    float _S2 = __ldg(&(ptr_0->y));

#line 51
    float _S3 = __ldg(&(ptr_0->z));

#line 51
    return make_float3 (_S1, _S2, _S3);
}


#line 10374 "hlsl.meta.slang"
static __device__ float dot_0(float2  x_0, float2  y_0)
{

#line 10401
    return x_0.x * y_0.x + x_0.y * y_0.y;
}


#line 10401
static __device__ GripTileMap_0 slang_ldg_1(GripTileMap_0 * ptr_1)
{

#line 10401
    float4  _S4 = __ldg(&ptr_1->u_0);

#line 10401
    float4  _S5 = __ldg(&ptr_1->v_0);

#line 10401
    GripTileMap_0 _S6 = { _S4, _S5 };

#line 10401
    return _S6;
}


#line 2840 "core.meta.slang"
static __device__ uint2  gripTileSize_0()
{

#line 2840
    float4  _S7 = __ldg(&globalParams_0->tile_0->destinationBounds_0);

#line 54 "../../../kernels/GripKernel.slang"
    return make_uint2 (uint(_S7.z - _S7.x), uint(_S7.w - _S7.y));
}


#line 54
static __device__ bool gripInsideTile_0(uint2  _S8)
{

#line 54
    uint2  _S9 = gripTileSize_0();

#line 54
    bool _S10;

#line 62
    if((_S8.x) < (_S9.x))
    {

#line 62
        _S10 = (_S8.y) < (_S9.y);

#line 62
    }
    else
    {

#line 62
        _S10 = false;

#line 62
    }

#line 62
    return _S10;
}


#line 62
static __device__ float2  gripImagePixel_0(uint2  _S11)
{

#line 62
    float4  _S12 = __ldg(&globalParams_0->tile_0->destinationBounds_0);

#line 62
    uint _S13 = __ldg(&globalParams_0->tile_0->reserved_0);

#line 70
    if((_S13 & 1U) != 0U)
    {
        return make_float2 (_S12.x + float(_S11.x) + 0.5f, _S12.y + float(_S11.y) + 0.5f);
    }
    return make_float2 (_S12.x + float(_S11.x) + 0.5f, _S12.w - float(_S11.y) - 0.5f);
}


#line 484 "core"
static __device__ float2  gripTexCoord_0(uint _S14, float2  _S15)
{

#line 81 "../../../kernels/GripKernel.slang"
    if(_S14 >= 16U)
    {
        return make_float2 (0.0f);
    }

#line 83
    GripTileMap_0 _S16 = slang_ldg_1(&globalParams_0->tile_0->textures_0[_S14]);

#line 83
    float4  _S17 = _S16.u_0;

#line 83
    float4  _S18 = _S16.v_0;


    return make_float2 (dot_0(float2 {_S17.x, _S17.y}, _S15) + _S16.u_0.z, dot_0(float2 {_S18.x, _S18.y}, _S15) + _S16.v_0.z);
}


#line 41 "GripBlur.slang"
extern "C" __global__ void fxGripKernel()
{

#line 41
    uint3  _S19 = blockIdx * blockDim + threadIdx;

    uint2  _S20 = uint2 {_S19.x, _S19.y};

#line 43
    bool _S21 = gripInsideTile_0(_S20);

#line 43
    if(!_S21)
    {
        return;
    }

#line 45
    float2  _S22 = gripImagePixel_0(_S20);

#line 45
    float2  _S23 = gripTexCoord_0(0U, _S22);



    uint width_0;

#line 49
    uint height_0;
    {uint32_t w, h; asm("txq.width.b32 %0, [%2]; txq.height.b32 %1, [%2];" : "=r"(w), "=r"(h) : "l"((globalParams_0->source_0))); *((&width_0)) = w;*((&height_0)) = h;};

#line 50
    float _S24 = __ldg(&globalParams_0->uniforms_0->radius_0);
    float2  _S25 = make_float2 (_S24 / float(width_0), 0.0f);
    int4  _S26 = make_int4 (int(0));

#line 52
    float4  _S27 = make_float4 ((float)_S26.x, (float)_S26.y, (float)_S26.z, (float)_S26.w);

#line 52
    int tap_0 = int(0);

#line 52
    float4  sum_0 = _S27;
    for(;;)
    {

#line 53
        if(tap_0 < (globalParams_0->TAPS_0))
        {
        }
        else
        {

#line 53
            break;
        }

#line 53
        float position_0;

        if((globalParams_0->TAPS_0) > int(1))
        {

#line 55
            position_0 = float(tap_0) / float(globalParams_0->TAPS_0 - int(1)) * 2.0f - 1.0f;

#line 55
        }
        else
        {

#line 55
            position_0 = 0.0f;

#line 55
        }
        float2  _S28 = _S23 + _S25 * make_float2 (position_0);

#line 56
        float4  sum_1 = sum_0 + (tex2DLod<float4 >((globalParams_0->source_0), ((_S28)).x, ((_S28)).y, ((0.0f))));

#line 53
        tap_0 = tap_0 + int(1);

#line 53
        sum_0 = sum_1;

#line 53
    }

#line 58
    float4  color_0 = sum_0 / make_float4 (float(globalParams_0->TAPS_0));

#line 58
    float2  _S29 = gripTexCoord_0(1U, _S22);
    float4  m_0 = (tex2DLod<float4 >((globalParams_0->matte_0), ((_S29)).x, ((_S29)).y, ((0.0f))));
    float3  _S30 = float3 {color_0.x, color_0.y, color_0.z};

#line 60
    float3  _S31 = slang_ldg_0(&globalParams_0->uniforms_0->tint_0);

#line 60
    float3  rgb_0 = _S30 * _S31 * make_float3 (m_0.w);

#line 60
    float3  rgb_1;
    if(globalParams_0->INVERT_0)
    {

#line 61
        rgb_1 = make_float3 (color_0.w) - rgb_0;

#line 61
    }
    else
    {

#line 61
        rgb_1 = rgb_0;

#line 61
    }



    surf2Dwrite<float4 >((make_float4 (rgb_1.x, rgb_1.y, rgb_1.z, color_0.w)), (globalParams_0->destination_0), ((_S20)).x * 16, ((_S20)).y, SLANG_CUDA_BOUNDARY_MODE);
    return;
}

