#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 60 "GripColorConvert.slang"
struct GripCurve_0
{
    float threshold_0;
    float slope_0;
    float offset_0;
    float scale_0;
    float exponent_0;
};

GripCurve_0 gripDecodeCurve_0(int transfer_0)
{
    thread GripCurve_0 curve_0;
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

GripCurve_0 gripEncodeCurve_0(int transfer_1)
{
    thread GripCurve_0 curve_1;
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
float gripPow_0(float base_0, float exponent_1)
{

#line 51
    float _S1;

    if(base_0 > 0.0f)
    {

#line 53
        _S1 = exp(exponent_1 * log(base_0));

#line 53
    }
    else
    {

#line 53
        _S1 = 0.0f;

#line 53
    }

#line 53
    return _S1;
}


#line 111
float gripDecode_0(int transfer_2, const GripCurve_0 thread* curve_2, float encoded_0)
{
    if(transfer_2 == int(0))
    {
        return encoded_0;
    }
    if(transfer_2 == int(7))
    {
        float p_0 = gripPow_0(clamp(encoded_0, 0.0f, 1.0f), 0.01268331333994865f);
        return gripPow_0(max(p_0 - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * p_0), 6.27739477157592773f);
    }

#line 120
    float linear_0;

    if(transfer_2 == int(8))
    {
        float e_0 = clamp(encoded_0, 0.0f, 1.0f);
        if(e_0 <= 0.5f)
        {

#line 125
            linear_0 = e_0 * e_0 / 3.0f;

#line 125
        }
        else
        {

#line 125
            linear_0 = (exp((e_0 - 0.55991071462631226f) / 0.1788327693939209f) + 0.28466892242431641f) / 12.0f;

#line 125
        }

#line 125
        return linear_0;
    }
    float magnitude_0 = abs(encoded_0);
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

float gripEncode_0(int transfer_3, const GripCurve_0 thread* curve_3, float linear_1)
{
    if(transfer_3 == int(0))
    {
        return linear_1;
    }
    if(transfer_3 == int(7))
    {
        float p_1 = gripPow_0(clamp(linear_1, 0.0f, 1.0f), 0.1593017578125f);
        return gripPow_0((0.8359375f + 18.8515625f * p_1) / (1.0f + 18.6875f * p_1), 78.84375f);
    }

#line 141
    float encoded_1;

    if(transfer_3 == int(8))
    {
        float l_0 = clamp(linear_1, 0.0f, 1.0f);
        if(l_0 <= 0.0833333358168602f)
        {

#line 146
            encoded_1 = sqrt(3.0f * l_0);

#line 146
        }
        else
        {

#line 146
            encoded_1 = 0.1788327693939209f * log(12.0f * l_0 - 0.28466892242431641f) + 0.55991071462631226f;

#line 146
        }

#line 146
        return encoded_1;
    }
    float magnitude_1 = abs(linear_1);
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


#line 29 "GripKernel.slang"
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


#line 24 "GripColorConvert.slang"
struct GripColorUniforms_0
{
    float3 row0_0;
    float3 row1_0;
    float3 row2_0;
    int decode_0;
    int encode_0;
    int premultiplied_0;
};




struct KernelContext_0
{
    GripTile_0 constant* tile_0;
    texture2d<float, access::sample> source_0;
    sampler bilinear_0;
    GripColorUniforms_0 constant* uniforms_0;
    texture2d<float, access::read_write> destination_0;
};


#line 2839 "core.meta.slang"
uint2 gripTileSize_0(KernelContext_0 thread* kernelContext_0)
{

#line 54 "GripKernel.slang"
    return uint2(uint(kernelContext_0->tile_0->destinationBounds_0.z - kernelContext_0->tile_0->destinationBounds_0.x), uint(kernelContext_0->tile_0->destinationBounds_0.w - kernelContext_0->tile_0->destinationBounds_0.y));
}


#line 54
bool gripInsideTile_0(uint2 _S2, KernelContext_0 thread* kernelContext_1)
{

#line 54
    uint2 _S3 = gripTileSize_0(kernelContext_1);

#line 54
    bool _S4;

#line 62
    if((_S2.x) < (_S3.x))
    {

#line 62
        _S4 = (_S2.y) < (_S3.y);

#line 62
    }
    else
    {

#line 62
        _S4 = false;

#line 62
    }

#line 62
    return _S4;
}


#line 62
float2 gripImagePixel_0(uint2 _S5, KernelContext_0 thread* kernelContext_2)
{

#line 62
    float4 d_0 = kernelContext_2->tile_0->destinationBounds_0;

#line 70
    if(((kernelContext_2->tile_0->reserved_0) & 1U) != 0U)
    {
        return float2(d_0.x + float(_S5.x) + 0.5f, d_0.y + float(_S5.y) + 0.5f);
    }
    return float2(d_0.x + float(_S5.x) + 0.5f, d_0.w - float(_S5.y) - 0.5f);
}


#line 74
float2 gripTexCoord_0(uint _S6, float2 _S7, KernelContext_0 thread* kernelContext_3)
{

#line 81
    if(_S6 >= 16U)
    {
        return float2(0.0f) ;
    }

    return float2(dot(kernelContext_3->tile_0->textures_0[_S6].u_0.xy, _S7) + kernelContext_3->tile_0->textures_0[_S6].u_0.z, dot(kernelContext_3->tile_0->textures_0[_S6].v_0.xy, _S7) + kernelContext_3->tile_0->textures_0[_S6].v_0.z);
}


#line 155 "GripColorConvert.slang"
[[kernel]] void gripColorConvert(uint3 tid_0 [[thread_position_in_grid]], GripTile_0 constant* tile_1 [[buffer(1)]], texture2d<float, access::sample> source_1 [[texture(0)]], sampler bilinear_1 [[sampler(0)]], GripColorUniforms_0 constant* uniforms_1 [[buffer(0)]], texture2d<float, access::read_write> destination_1 [[texture(1)]])
{

#line 155
    thread KernelContext_0 kernelContext_4;

#line 155
    (&kernelContext_4)->tile_0 = tile_1;

#line 155
    (&kernelContext_4)->source_0 = source_1;

#line 155
    (&kernelContext_4)->bilinear_0 = bilinear_1;

#line 155
    (&kernelContext_4)->uniforms_0 = uniforms_1;

#line 155
    (&kernelContext_4)->destination_0 = destination_1;

    uint2 _S8 = tid_0.xy;

#line 157
    bool _S9 = gripInsideTile_0(_S8, &kernelContext_4);

#line 157
    if(!_S9)
    {
        return;
    }

#line 159
    float2 _S10 = gripImagePixel_0(_S8, &kernelContext_4);

#line 159
    float2 _S11 = gripTexCoord_0(0U, _S10, &kernelContext_4);


    float4 color_0 = (((&kernelContext_4)->source_0).sample(((&kernelContext_4)->bilinear_0), (_S11), level((0.0f))));

#line 162
    bool curved_0;


    if(((&kernelContext_4)->uniforms_0->decode_0) != int(0))
    {

#line 165
        curved_0 = true;

#line 165
    }
    else
    {

#line 165
        curved_0 = ((&kernelContext_4)->uniforms_0->encode_0) != int(0);

#line 165
    }
    if(curved_0)
    {

#line 166
        curved_0 = ((&kernelContext_4)->uniforms_0->premultiplied_0) != int(0);

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
    float3 rgb_0;
    if(unpremultiply_0)
    {

#line 167
        rgb_0 = color_0.xyz / float3(color_0.w) ;

#line 167
    }
    else
    {

#line 167
        rgb_0 = color_0.xyz;

#line 167
    }
    GripCurve_0 decode_1 = gripDecodeCurve_0((&kernelContext_4)->uniforms_0->decode_0);
    GripCurve_0 encode_1 = gripEncodeCurve_0((&kernelContext_4)->uniforms_0->encode_0);
    int _S12 = (&kernelContext_4)->uniforms_0->decode_0;

#line 170
    float _S13 = rgb_0.x;

#line 170
    thread GripCurve_0 _S14 = decode_1;

#line 170
    float _S15 = gripDecode_0(_S12, &_S14, _S13);

#line 170
    int _S16 = (&kernelContext_4)->uniforms_0->decode_0;

#line 170
    float _S17 = rgb_0.y;

#line 170
    thread GripCurve_0 _S18 = decode_1;

#line 170
    float _S19 = gripDecode_0(_S16, &_S18, _S17);

#line 170
    int _S20 = (&kernelContext_4)->uniforms_0->decode_0;

#line 170
    float _S21 = rgb_0.z;

#line 170
    thread GripCurve_0 _S22 = decode_1;

#line 170
    float _S23 = gripDecode_0(_S20, &_S22, _S21);

#line 170
    float3 rgb_1 = float3(_S15, _S19, _S23);
    float _S24 = dot((&kernelContext_4)->uniforms_0->row0_0, rgb_1);

#line 171
    float _S25 = dot((&kernelContext_4)->uniforms_0->row1_0, rgb_1);

#line 171
    float _S26 = dot((&kernelContext_4)->uniforms_0->row2_0, rgb_1);
    int _S27 = (&kernelContext_4)->uniforms_0->encode_0;

#line 172
    thread GripCurve_0 _S28 = encode_1;

#line 172
    float _S29 = gripEncode_0(_S27, &_S28, _S24);

#line 172
    int _S30 = (&kernelContext_4)->uniforms_0->encode_0;

#line 172
    thread GripCurve_0 _S31 = encode_1;

#line 172
    float _S32 = gripEncode_0(_S30, &_S31, _S25);

#line 172
    int _S33 = (&kernelContext_4)->uniforms_0->encode_0;

#line 172
    thread GripCurve_0 _S34 = encode_1;

#line 172
    float _S35 = gripEncode_0(_S33, &_S34, _S26);

#line 172
    float3 rgb_2 = float3(_S29, _S32, _S35);
    if(unpremultiply_0)
    {

#line 173
        rgb_0 = rgb_2 * float3(color_0.w) ;

#line 173
    }
    else
    {

#line 173
        rgb_0 = rgb_2;

#line 173
    }



    (&kernelContext_4)->destination_0.write(float4(rgb_0, color_0.w),_S8);
    return;
}

