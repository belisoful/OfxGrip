#include "slang-cuda-prelude.h"


#line 24 "GripColorConvert.slang"
struct GripColorUniforms_0
{
    float3  row0_0;
    float3  row1_0;
    float3  row2_0;
    int decode_0;
    int encode_0;
    int premultiplied_0;
};


#line 29 "GripKernel.slang"
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


#line 124 "GripColorConvert.slang"
struct GlobalParams_0
{
    GripColorUniforms_0* uniforms_0;
    GripTile_0* tile_0;
    CUtexObject source_0;
    CUsurfObject destination_0;
    SamplerState bilinear_0;
};


#line 124
extern "C" __constant__ GlobalParams_0 SLANG_globalParams;
#define globalParams_0 (&SLANG_globalParams)

#line 10374 "hlsl.meta.slang"
static __device__ float dot_0(float3  x_0, float3  y_0)
{

#line 10401
    return x_0.x * y_0.x + x_0.y * y_0.y + x_0.z * y_0.z;
}


#line 9341
static __device__ float clamp_0(float x_1, float minBound_0, float maxBound_0)
{

#line 9353
    return (F32_min(((F32_max((x_1), (minBound_0)))), (maxBound_0)));
}


#line 129 "GripColorConvert.slang"
static __device__ float3  slang_ldg_0(float3  * ptr_0)
{

#line 129
    float _S1 = __ldg(&(ptr_0->x));

#line 129
    float _S2 = __ldg(&(ptr_0->y));

#line 129
    float _S3 = __ldg(&(ptr_0->z));

#line 129
    return make_float3 (_S1, _S2, _S3);
}


#line 10374 "hlsl.meta.slang"
static __device__ float dot_1(float2  x_2, float2  y_1)
{

#line 10401
    return x_2.x * y_1.x + x_2.y * y_1.y;
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


#line 49 "GripColorConvert.slang"
static __device__ float gripDecodeMagnitude_0(int transfer_0, float e_0)
{

#line 49
    float _S7;

    switch(transfer_0)
    {
    case int(1):
        {

#line 53
            if(e_0 <= 0.04044999927282333f)
            {

#line 53
                _S7 = e_0 / 12.92000007629394531f;

#line 53
            }
            else
            {

#line 53
                _S7 = (F32_pow(((e_0 + 0.05499999970197678f) / 1.0549999475479126f), (2.40000009536743164f)));

#line 53
            }

#line 53
            return _S7;
        }
    case int(2):
        {

#line 54
            if(e_0 < 0.08100000023841858f)
            {

#line 54
                _S7 = e_0 / 4.5f;

#line 54
            }
            else
            {

#line 54
                _S7 = (F32_pow(((e_0 + 0.0989999994635582f) / 1.09899997711181641f), (2.22222232818603516f)));

#line 54
            }

#line 54
            return _S7;
        }
    case int(3):
        {

#line 55
            return (F32_pow((e_0), (1.79999995231628418f)));
        }
    case int(4):
        {

#line 56
            return (F32_pow((e_0), (2.20000004768371582f)));
        }
    case int(5):
        {

#line 57
            return (F32_pow((e_0), (2.40000009536743164f)));
        }
    case int(6):
        {

#line 58
            return (F32_pow((e_0), (2.59999990463256836f)));
        }
    default:
        {

#line 59
            return e_0;
        }
    }

#line 59
}



static __device__ float gripDecode_0(int transfer_1, float encoded_0)
{
    if(transfer_1 == int(0))
    {
        return encoded_0;
    }
    if(transfer_1 == int(7))
    {
        float p_0 = (F32_pow((clamp_0(encoded_0, 0.0f, 1.0f)), (0.01268331333994865f)));
        return (F32_pow(((F32_max((p_0 - 0.8359375f), (0.0f))) / (18.8515625f - 18.6875f * p_0)), (6.27739477157592773f)));
    }

#line 72
    float _S8;

    if(transfer_1 == int(8))
    {
        float e_1 = clamp_0(encoded_0, 0.0f, 1.0f);
        if(e_1 <= 0.5f)
        {

#line 77
            _S8 = e_1 * e_1 / 3.0f;

#line 77
        }
        else
        {

#line 77
            _S8 = ((F32_exp(((e_1 - 0.55991071462631226f) / 0.1788327693939209f))) + 0.28466892242431641f) / 12.0f;

#line 77
        }

#line 77
        return _S8;
    }
    if(encoded_0 < 0.0f)
    {

#line 79
        _S8 = - gripDecodeMagnitude_0(transfer_1, - encoded_0);

#line 79
    }
    else
    {

#line 79
        _S8 = gripDecodeMagnitude_0(transfer_1, encoded_0);

#line 79
    }

#line 79
    return _S8;
}

static __device__ float gripEncodeMagnitude_0(int transfer_2, float l_0)
{

#line 82
    float _S9;

    switch(transfer_2)
    {
    case int(1):
        {

#line 86
            if(l_0 <= 0.00313080009073019f)
            {

#line 86
                _S9 = l_0 * 12.92000007629394531f;

#line 86
            }
            else
            {

#line 86
                _S9 = 1.0549999475479126f * (F32_pow((l_0), (0.4166666567325592f))) - 0.05499999970197678f;

#line 86
            }

#line 86
            return _S9;
        }
    case int(2):
        {

#line 87
            if(l_0 < 0.01799999922513962f)
            {

#line 87
                _S9 = l_0 * 4.5f;

#line 87
            }
            else
            {

#line 87
                _S9 = 1.09899997711181641f * (F32_pow((l_0), (0.44999998807907104f))) - 0.0989999994635582f;

#line 87
            }

#line 87
            return _S9;
        }
    case int(3):
        {

#line 88
            return (F32_pow((l_0), (0.55555558204650879f)));
        }
    case int(4):
        {

#line 89
            return (F32_pow((l_0), (0.45454543828964233f)));
        }
    case int(5):
        {

#line 90
            return (F32_pow((l_0), (0.4166666567325592f)));
        }
    case int(6):
        {

#line 91
            return (F32_pow((l_0), (0.38461539149284363f)));
        }
    default:
        {

#line 92
            return l_0;
        }
    }

#line 92
}



static __device__ float gripEncode_0(int transfer_3, float linear_0)
{
    if(transfer_3 == int(0))
    {
        return linear_0;
    }
    if(transfer_3 == int(7))
    {
        float p_1 = (F32_pow((clamp_0(linear_0, 0.0f, 1.0f)), (0.1593017578125f)));
        return (F32_pow(((0.8359375f + 18.8515625f * p_1) / (1.0f + 18.6875f * p_1)), (78.84375f)));
    }

#line 105
    float _S10;

    if(transfer_3 == int(8))
    {
        float l_1 = clamp_0(linear_0, 0.0f, 1.0f);
        if(l_1 <= 0.0833333358168602f)
        {

#line 110
            _S10 = (F32_sqrt((3.0f * l_1)));

#line 110
        }
        else
        {

#line 110
            _S10 = 0.1788327693939209f * (F32_log((12.0f * l_1 - 0.28466892242431641f))) + 0.55991071462631226f;

#line 110
        }

#line 110
        return _S10;
    }
    if(linear_0 < 0.0f)
    {

#line 112
        _S10 = - gripEncodeMagnitude_0(transfer_3, - linear_0);

#line 112
    }
    else
    {

#line 112
        _S10 = gripEncodeMagnitude_0(transfer_3, linear_0);

#line 112
    }

#line 112
    return _S10;
}


#line 2840 "core.meta.slang"
static __device__ uint2  gripTileSize_0()
{

#line 2840
    float4  _S11 = __ldg(&globalParams_0->tile_0->destinationBounds_0);

#line 54 "GripKernel.slang"
    return make_uint2 (uint(_S11.z - _S11.x), uint(_S11.w - _S11.y));
}


#line 54
static __device__ bool gripInsideTile_0(uint2  _S12)
{

#line 54
    uint2  _S13 = gripTileSize_0();

#line 54
    bool _S14;

#line 62
    if((_S12.x) < (_S13.x))
    {

#line 62
        _S14 = (_S12.y) < (_S13.y);

#line 62
    }
    else
    {

#line 62
        _S14 = false;

#line 62
    }

#line 62
    return _S14;
}


#line 62
static __device__ float2  gripImagePixel_0(uint2  _S15)
{

#line 62
    float4  _S16 = __ldg(&globalParams_0->tile_0->destinationBounds_0);

#line 62
    uint _S17 = __ldg(&globalParams_0->tile_0->reserved_0);

#line 70
    if((_S17 & 1U) != 0U)
    {
        return make_float2 (_S16.x + float(_S15.x) + 0.5f, _S16.y + float(_S15.y) + 0.5f);
    }
    return make_float2 (_S16.x + float(_S15.x) + 0.5f, _S16.w - float(_S15.y) - 0.5f);
}


#line 74
static __device__ float2  gripTexCoord_0(uint _S18, float2  _S19)
{

#line 81
    if(_S18 >= 16U)
    {
        return make_float2 (0.0f);
    }

#line 83
    GripTileMap_0 _S20 = slang_ldg_1(&globalParams_0->tile_0->textures_0[_S18]);

#line 83
    float4  _S21 = _S20.u_0;

#line 83
    float4  _S22 = _S20.v_0;


    return make_float2 (dot_1(float2 {_S21.x, _S21.y}, _S19) + _S20.u_0.z, dot_1(float2 {_S22.x, _S22.y}, _S19) + _S20.v_0.z);
}


#line 117 "GripColorConvert.slang"
extern "C" __global__ void gripColorConvert()
{

#line 117
    uint3  _S23 = blockIdx * blockDim + threadIdx;

    uint2  _S24 = uint2 {_S23.x, _S23.y};

#line 119
    bool _S25 = gripInsideTile_0(_S24);

#line 119
    if(!_S25)
    {
        return;
    }

#line 121
    float2  _S26 = gripImagePixel_0(_S24);

#line 121
    float2  _S27 = gripTexCoord_0(0U, _S26);


    float4  color_0 = (tex2DLod<float4 >((globalParams_0->source_0), ((_S27)).x, ((_S27)).y, ((0.0f))));

#line 124
    int _S28 = __ldg(&globalParams_0->uniforms_0->decode_0);

#line 124
    bool curved_0;


    if(_S28 != int(0))
    {

#line 127
        curved_0 = true;

#line 127
    }
    else
    {

#line 127
        int _S29 = __ldg(&globalParams_0->uniforms_0->encode_0);

#line 127
        curved_0 = _S29 != int(0);

#line 127
    }
    if(curved_0)
    {

#line 128
        int _S30 = __ldg(&globalParams_0->uniforms_0->premultiplied_0);

#line 128
        curved_0 = _S30 != int(0);

#line 128
    }
    else
    {

#line 128
        curved_0 = false;

#line 128
    }

#line 128
    bool unpremultiply_0;

#line 128
    if(curved_0)
    {

#line 128
        unpremultiply_0 = (color_0.w) > 0.0f;

#line 128
    }
    else
    {

#line 128
        unpremultiply_0 = false;

#line 128
    }

#line 128
    float3  rgb_0;
    if(unpremultiply_0)
    {

#line 129
        rgb_0 = float3 {color_0.x, color_0.y, color_0.z} / make_float3 (color_0.w);

#line 129
    }
    else
    {

#line 129
        rgb_0 = float3 {color_0.x, color_0.y, color_0.z};

#line 129
    }

#line 129
    int _S31 = __ldg(&globalParams_0->uniforms_0->decode_0);
    float _S32 = gripDecode_0(_S31, rgb_0.x);

#line 130
    int _S33 = __ldg(&globalParams_0->uniforms_0->decode_0);

#line 130
    float _S34 = gripDecode_0(_S33, rgb_0.y);

#line 130
    int _S35 = __ldg(&globalParams_0->uniforms_0->decode_0);

#line 130
    float3  rgb_1 = make_float3 (_S32, _S34, gripDecode_0(_S35, rgb_0.z));

#line 130
    float3  _S36 = slang_ldg_0(&globalParams_0->uniforms_0->row0_0);
    float _S37 = dot_0(_S36, rgb_1);

#line 131
    float3  _S38 = slang_ldg_0(&globalParams_0->uniforms_0->row1_0);

#line 131
    float _S39 = dot_0(_S38, rgb_1);

#line 131
    float3  _S40 = slang_ldg_0(&globalParams_0->uniforms_0->row2_0);

#line 131
    float _S41 = dot_0(_S40, rgb_1);

#line 131
    int _S42 = __ldg(&globalParams_0->uniforms_0->encode_0);
    float _S43 = gripEncode_0(_S42, _S37);

#line 132
    int _S44 = __ldg(&globalParams_0->uniforms_0->encode_0);

#line 132
    float _S45 = gripEncode_0(_S44, _S39);

#line 132
    int _S46 = __ldg(&globalParams_0->uniforms_0->encode_0);

#line 132
    float3  rgb_2 = make_float3 (_S43, _S45, gripEncode_0(_S46, _S41));
    if(unpremultiply_0)
    {

#line 133
        rgb_0 = rgb_2 * make_float3 (color_0.w);

#line 133
    }
    else
    {

#line 133
        rgb_0 = rgb_2;

#line 133
    }



    surf2Dwrite<float4 >((make_float4 (rgb_0.x, rgb_0.y, rgb_0.z, color_0.w)), (globalParams_0->destination_0), ((_S24)).x * 16, ((_S24)).y, SLANG_CUDA_BOUNDARY_MODE);
    return;
}

