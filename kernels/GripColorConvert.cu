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


#line 162 "GripColorConvert.slang"
struct GlobalParams_0
{
    GripColorUniforms_0* uniforms_0;
    GripTile_0* tile_0;
    CUtexObject source_0;
    CUsurfObject destination_0;
    SamplerState bilinear_0;
};


#line 162
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


#line 72 "GripColorConvert.slang"
static __device__ float3  slang_ldg_0(float3  * ptr_0)
{

#line 72
    float _S1 = __ldg(&(ptr_0->x));

#line 72
    float _S2 = __ldg(&(ptr_0->y));

#line 72
    float _S3 = __ldg(&(ptr_0->z));

#line 72
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


#line 60 "GripColorConvert.slang"
struct GripCurve_0
{
    float threshold_0;
    float slope_0;
    float offset_0;
    float scale_0;
    float exponent_0;
};

static __device__ GripCurve_0 gripDecodeCurve_0(int transfer_0)
{
    GripCurve_0 curve_0;
    (&curve_0)->threshold_0 = -1.0f;
    (&curve_0)->slope_0 = 1.0f;
    (&curve_0)->offset_0 = 0.0f;
    (&curve_0)->scale_0 = 1.0f;
    (&curve_0)->exponent_0 = 1.0f;
    switch(transfer_0)
    {
    case int(1):
        {

#line 79
            (&curve_0)->threshold_0 = 0.04044999927282333f;

#line 79
            (&curve_0)->slope_0 = 0.07739938050508499f;

#line 79
            (&curve_0)->offset_0 = 0.05499999970197678f;

#line 79
            (&curve_0)->scale_0 = 0.94786733388900757f;

#line 79
            (&curve_0)->exponent_0 = 2.40000009536743164f;

#line 79
            break;
        }
    case int(2):
        {

#line 80
            (&curve_0)->threshold_0 = 0.08100000023841858f;

#line 80
            (&curve_0)->slope_0 = 0.2222222238779068f;

#line 80
            (&curve_0)->offset_0 = 0.0989999994635582f;

#line 80
            (&curve_0)->scale_0 = 0.90991812944412231f;

#line 80
            (&curve_0)->exponent_0 = 2.22222232818603516f;

#line 80
            break;
        }
    case int(3):
        {

#line 81
            (&curve_0)->exponent_0 = 1.79999995231628418f;

#line 81
            break;
        }
    case int(4):
        {

#line 82
            (&curve_0)->exponent_0 = 2.20000004768371582f;

#line 82
            break;
        }
    case int(5):
        {

#line 83
            (&curve_0)->exponent_0 = 2.40000009536743164f;

#line 83
            break;
        }
    case int(6):
        {

#line 84
            (&curve_0)->exponent_0 = 2.59999990463256836f;

#line 84
            break;
        }
    default:
        {

#line 85
            break;
        }
    }

#line 87
    return curve_0;
}

static __device__ GripCurve_0 gripEncodeCurve_0(int transfer_1)
{
    GripCurve_0 curve_1;
    (&curve_1)->threshold_0 = -1.0f;
    (&curve_1)->slope_0 = 1.0f;
    (&curve_1)->offset_0 = 0.0f;
    (&curve_1)->scale_0 = 1.0f;
    (&curve_1)->exponent_0 = 1.0f;
    switch(transfer_1)
    {
    case int(1):
        {

#line 100
            (&curve_1)->threshold_0 = 0.00313080009073019f;

#line 100
            (&curve_1)->slope_0 = 12.92000007629394531f;

#line 100
            (&curve_1)->offset_0 = 0.05499999970197678f;

#line 100
            (&curve_1)->scale_0 = 1.0549999475479126f;

#line 100
            (&curve_1)->exponent_0 = 0.4166666567325592f;

#line 100
            break;
        }
    case int(2):
        {

#line 101
            (&curve_1)->threshold_0 = 0.01799999922513962f;

#line 101
            (&curve_1)->slope_0 = 4.5f;

#line 101
            (&curve_1)->offset_0 = 0.0989999994635582f;

#line 101
            (&curve_1)->scale_0 = 1.09899997711181641f;

#line 101
            (&curve_1)->exponent_0 = 0.44999998807907104f;

#line 101
            break;
        }
    case int(3):
        {

#line 102
            (&curve_1)->exponent_0 = 0.55555558204650879f;

#line 102
            break;
        }
    case int(4):
        {

#line 103
            (&curve_1)->exponent_0 = 0.45454543828964233f;

#line 103
            break;
        }
    case int(5):
        {

#line 104
            (&curve_1)->exponent_0 = 0.4166666567325592f;

#line 104
            break;
        }
    case int(6):
        {

#line 105
            (&curve_1)->exponent_0 = 0.38461539149284363f;

#line 105
            break;
        }
    default:
        {

#line 106
            break;
        }
    }

#line 108
    return curve_1;
}


#line 51
static __device__ float gripPow_0(float base_0, float exponent_1)
{

#line 51
    float _S7;

    if(base_0 > 0.0f)
    {

#line 53
        _S7 = (F32_exp((exponent_1 * (F32_log((base_0))))));

#line 53
    }
    else
    {

#line 53
        _S7 = 0.0f;

#line 53
    }

#line 53
    return _S7;
}


#line 111
static __device__ float gripDecode_0(int transfer_2, GripCurve_0 * curve_2, float encoded_0)
{
    if(transfer_2 == int(0))
    {
        return encoded_0;
    }
    if(transfer_2 == int(7))
    {
        float p_0 = gripPow_0(clamp_0(encoded_0, 0.0f, 1.0f), 0.01268331333994865f);
        return gripPow_0((F32_max((p_0 - 0.8359375f), (0.0f))) / (18.8515625f - 18.6875f * p_0), 6.27739477157592773f);
    }

#line 120
    float linear_0;

    if(transfer_2 == int(8))
    {
        float e_0 = clamp_0(encoded_0, 0.0f, 1.0f);
        if(e_0 <= 0.5f)
        {

#line 125
            linear_0 = e_0 * e_0 / 3.0f;

#line 125
        }
        else
        {

#line 125
            linear_0 = ((F32_exp(((e_0 - 0.55991071462631226f) / 0.1788327693939209f))) + 0.28466892242431641f) / 12.0f;

#line 125
        }

#line 125
        return linear_0;
    }
    float magnitude_0 = (F32_abs((encoded_0)));
    if(magnitude_0 <= (curve_2->threshold_0))
    {

#line 128
        linear_0 = magnitude_0 * curve_2->slope_0;

#line 128
    }
    else
    {

#line 128
        linear_0 = gripPow_0((magnitude_0 + curve_2->offset_0) * curve_2->scale_0, curve_2->exponent_0);

#line 128
    }
    if(encoded_0 < 0.0f)
    {

#line 129
        linear_0 = - linear_0;

#line 129
    }

#line 129
    return linear_0;
}

static __device__ float gripEncode_0(int transfer_3, GripCurve_0 * curve_3, float linear_1)
{
    if(transfer_3 == int(0))
    {
        return linear_1;
    }
    if(transfer_3 == int(7))
    {
        float p_1 = gripPow_0(clamp_0(linear_1, 0.0f, 1.0f), 0.1593017578125f);
        return gripPow_0((0.8359375f + 18.8515625f * p_1) / (1.0f + 18.6875f * p_1), 78.84375f);
    }

#line 141
    float encoded_1;

    if(transfer_3 == int(8))
    {
        float l_0 = clamp_0(linear_1, 0.0f, 1.0f);
        if(l_0 <= 0.0833333358168602f)
        {

#line 146
            encoded_1 = (F32_sqrt((3.0f * l_0)));

#line 146
        }
        else
        {

#line 146
            encoded_1 = 0.1788327693939209f * (F32_log((12.0f * l_0 - 0.28466892242431641f))) + 0.55991071462631226f;

#line 146
        }

#line 146
        return encoded_1;
    }
    float magnitude_1 = (F32_abs((linear_1)));
    if(magnitude_1 <= (curve_3->threshold_0))
    {

#line 149
        encoded_1 = magnitude_1 * curve_3->slope_0;

#line 149
    }
    else
    {

#line 149
        encoded_1 = curve_3->scale_0 * gripPow_0(magnitude_1, curve_3->exponent_0) - curve_3->offset_0;

#line 149
    }
    if(linear_1 < 0.0f)
    {

#line 150
        encoded_1 = - encoded_1;

#line 150
    }

#line 150
    return encoded_1;
}


#line 2840 "core.meta.slang"
static __device__ uint2  gripTileSize_0()
{

#line 2840
    float4  _S8 = __ldg(&globalParams_0->tile_0->destinationBounds_0);

#line 54 "GripKernel.slang"
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


#line 74
static __device__ float2  gripTexCoord_0(uint _S15, float2  _S16)
{

#line 81
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


    return make_float2 (dot_1(float2 {_S18.x, _S18.y}, _S16) + _S17.u_0.z, dot_1(float2 {_S19.x, _S19.y}, _S16) + _S17.v_0.z);
}


#line 155 "GripColorConvert.slang"
extern "C" __global__ void gripColorConvert()
{

#line 155
    uint3  _S20 = blockIdx * blockDim + threadIdx;

    uint2  _S21 = uint2 {_S20.x, _S20.y};

#line 157
    bool _S22 = gripInsideTile_0(_S21);

#line 157
    if(!_S22)
    {
        return;
    }

#line 159
    float2  _S23 = gripImagePixel_0(_S21);

#line 159
    float2  _S24 = gripTexCoord_0(0U, _S23);


    float4  color_0 = (tex2DLod<float4 >((globalParams_0->source_0), ((_S24)).x, ((_S24)).y, ((0.0f))));

#line 162
    int _S25 = __ldg(&globalParams_0->uniforms_0->decode_0);

#line 162
    bool curved_0;


    if(_S25 != int(0))
    {

#line 165
        curved_0 = true;

#line 165
    }
    else
    {

#line 165
        int _S26 = __ldg(&globalParams_0->uniforms_0->encode_0);

#line 165
        curved_0 = _S26 != int(0);

#line 165
    }
    if(curved_0)
    {

#line 166
        int _S27 = __ldg(&globalParams_0->uniforms_0->premultiplied_0);

#line 166
        curved_0 = _S27 != int(0);

#line 166
    }
    else
    {

#line 166
        curved_0 = false;

#line 166
    }

#line 166
    bool unpremultiply_0;

#line 166
    if(curved_0)
    {

#line 166
        unpremultiply_0 = (color_0.w) > 0.0f;

#line 166
    }
    else
    {

#line 166
        unpremultiply_0 = false;

#line 166
    }

#line 166
    float3  rgb_0;
    if(unpremultiply_0)
    {

#line 167
        rgb_0 = float3 {color_0.x, color_0.y, color_0.z} / make_float3 (color_0.w);

#line 167
    }
    else
    {

#line 167
        rgb_0 = float3 {color_0.x, color_0.y, color_0.z};

#line 167
    }

#line 167
    int _S28 = __ldg(&globalParams_0->uniforms_0->decode_0);
    GripCurve_0 decode_1 = gripDecodeCurve_0(_S28);

#line 168
    int _S29 = __ldg(&globalParams_0->uniforms_0->encode_0);
    GripCurve_0 encode_1 = gripEncodeCurve_0(_S29);

#line 169
    int _S30 = __ldg(&globalParams_0->uniforms_0->decode_0);
    float _S31 = rgb_0.x;

#line 170
    GripCurve_0 _S32 = decode_1;

#line 170
    float _S33 = gripDecode_0(_S30, &_S32, _S31);

#line 170
    int _S34 = __ldg(&globalParams_0->uniforms_0->decode_0);

#line 170
    float _S35 = rgb_0.y;

#line 170
    GripCurve_0 _S36 = decode_1;

#line 170
    float _S37 = gripDecode_0(_S34, &_S36, _S35);

#line 170
    int _S38 = __ldg(&globalParams_0->uniforms_0->decode_0);

#line 170
    float _S39 = rgb_0.z;

#line 170
    GripCurve_0 _S40 = decode_1;

#line 170
    float _S41 = gripDecode_0(_S38, &_S40, _S39);

#line 170
    float3  rgb_1 = make_float3 (_S33, _S37, _S41);

#line 170
    float3  _S42 = slang_ldg_0(&globalParams_0->uniforms_0->row0_0);
    float _S43 = dot_0(_S42, rgb_1);

#line 171
    float3  _S44 = slang_ldg_0(&globalParams_0->uniforms_0->row1_0);

#line 171
    float _S45 = dot_0(_S44, rgb_1);

#line 171
    float3  _S46 = slang_ldg_0(&globalParams_0->uniforms_0->row2_0);

#line 171
    float _S47 = dot_0(_S46, rgb_1);

#line 171
    int _S48 = __ldg(&globalParams_0->uniforms_0->encode_0);

#line 171
    GripCurve_0 _S49 = encode_1;

#line 171
    float _S50 = gripEncode_0(_S48, &_S49, _S43);

#line 171
    int _S51 = __ldg(&globalParams_0->uniforms_0->encode_0);

#line 171
    GripCurve_0 _S52 = encode_1;

#line 171
    float _S53 = gripEncode_0(_S51, &_S52, _S45);

#line 171
    int _S54 = __ldg(&globalParams_0->uniforms_0->encode_0);

#line 171
    GripCurve_0 _S55 = encode_1;

#line 171
    float _S56 = gripEncode_0(_S54, &_S55, _S47);
    float3  rgb_2 = make_float3 (_S50, _S53, _S56);
    if(unpremultiply_0)
    {

#line 173
        rgb_0 = rgb_2 * make_float3 (color_0.w);

#line 173
    }
    else
    {

#line 173
        rgb_0 = rgb_2;

#line 173
    }



    surf2Dwrite<float4 >((make_float4 (rgb_0.x, rgb_0.y, rgb_0.z, color_0.w)), (globalParams_0->destination_0), ((_S21)).x * 16, ((_S21)).y, SLANG_CUDA_BOUNDARY_MODE);
    return;
}

