#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 49 "GripColorConvert.slang"
float gripDecodeMagnitude_0(int transfer_0, float e_0)
{

#line 49
    float _S1;

    switch(transfer_0)
    {
    case int(1):
        {

#line 53
            if(e_0 <= 0.04044999927282333f)
            {

#line 53
                _S1 = e_0 / 12.92000007629394531f;

#line 53
            }
            else
            {

#line 53
                _S1 = pow((e_0 + 0.05499999970197678f) / 1.0549999475479126f, 2.40000009536743164f);

#line 53
            }

#line 53
            return _S1;
        }
    case int(2):
        {

#line 54
            if(e_0 < 0.08100000023841858f)
            {

#line 54
                _S1 = e_0 / 4.5f;

#line 54
            }
            else
            {

#line 54
                _S1 = pow((e_0 + 0.0989999994635582f) / 1.09899997711181641f, 2.22222232818603516f);

#line 54
            }

#line 54
            return _S1;
        }
    case int(3):
        {

#line 55
            return pow(e_0, 1.79999995231628418f);
        }
    case int(4):
        {

#line 56
            return pow(e_0, 2.20000004768371582f);
        }
    case int(5):
        {

#line 57
            return pow(e_0, 2.40000009536743164f);
        }
    case int(6):
        {

#line 58
            return pow(e_0, 2.59999990463256836f);
        }
    default:
        {

#line 59
            return e_0;
        }
    }

#line 59
}



float gripDecode_0(int transfer_1, float encoded_0)
{
    if(transfer_1 == int(0))
    {
        return encoded_0;
    }
    if(transfer_1 == int(7))
    {
        float p_0 = pow(clamp(encoded_0, 0.0f, 1.0f), 0.01268331333994865f);
        return pow(max(p_0 - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * p_0), 6.27739477157592773f);
    }

#line 72
    float _S2;

    if(transfer_1 == int(8))
    {
        float e_1 = clamp(encoded_0, 0.0f, 1.0f);
        if(e_1 <= 0.5f)
        {

#line 77
            _S2 = e_1 * e_1 / 3.0f;

#line 77
        }
        else
        {

#line 77
            _S2 = (exp((e_1 - 0.55991071462631226f) / 0.1788327693939209f) + 0.28466892242431641f) / 12.0f;

#line 77
        }

#line 77
        return _S2;
    }
    if(encoded_0 < 0.0f)
    {

#line 79
        _S2 = - gripDecodeMagnitude_0(transfer_1, - encoded_0);

#line 79
    }
    else
    {

#line 79
        _S2 = gripDecodeMagnitude_0(transfer_1, encoded_0);

#line 79
    }

#line 79
    return _S2;
}

float gripEncodeMagnitude_0(int transfer_2, float l_0)
{

#line 82
    float _S3;

    switch(transfer_2)
    {
    case int(1):
        {

#line 86
            if(l_0 <= 0.00313080009073019f)
            {

#line 86
                _S3 = l_0 * 12.92000007629394531f;

#line 86
            }
            else
            {

#line 86
                _S3 = 1.0549999475479126f * pow(l_0, 0.4166666567325592f) - 0.05499999970197678f;

#line 86
            }

#line 86
            return _S3;
        }
    case int(2):
        {

#line 87
            if(l_0 < 0.01799999922513962f)
            {

#line 87
                _S3 = l_0 * 4.5f;

#line 87
            }
            else
            {

#line 87
                _S3 = 1.09899997711181641f * pow(l_0, 0.44999998807907104f) - 0.0989999994635582f;

#line 87
            }

#line 87
            return _S3;
        }
    case int(3):
        {

#line 88
            return pow(l_0, 0.55555558204650879f);
        }
    case int(4):
        {

#line 89
            return pow(l_0, 0.45454543828964233f);
        }
    case int(5):
        {

#line 90
            return pow(l_0, 0.4166666567325592f);
        }
    case int(6):
        {

#line 91
            return pow(l_0, 0.38461539149284363f);
        }
    default:
        {

#line 92
            return l_0;
        }
    }

#line 92
}



float gripEncode_0(int transfer_3, float linear_0)
{
    if(transfer_3 == int(0))
    {
        return linear_0;
    }
    if(transfer_3 == int(7))
    {
        float p_1 = pow(clamp(linear_0, 0.0f, 1.0f), 0.1593017578125f);
        return pow((0.8359375f + 18.8515625f * p_1) / (1.0f + 18.6875f * p_1), 78.84375f);
    }

#line 105
    float _S4;

    if(transfer_3 == int(8))
    {
        float l_1 = clamp(linear_0, 0.0f, 1.0f);
        if(l_1 <= 0.0833333358168602f)
        {

#line 110
            _S4 = sqrt(3.0f * l_1);

#line 110
        }
        else
        {

#line 110
            _S4 = 0.1788327693939209f * log(12.0f * l_1 - 0.28466892242431641f) + 0.55991071462631226f;

#line 110
        }

#line 110
        return _S4;
    }
    if(linear_0 < 0.0f)
    {

#line 112
        _S4 = - gripEncodeMagnitude_0(transfer_3, - linear_0);

#line 112
    }
    else
    {

#line 112
        _S4 = gripEncodeMagnitude_0(transfer_3, linear_0);

#line 112
    }

#line 112
    return _S4;
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
bool gripInsideTile_0(uint2 _S5, KernelContext_0 thread* kernelContext_1)
{

#line 54
    uint2 _S6 = gripTileSize_0(kernelContext_1);

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
float2 gripImagePixel_0(uint2 _S8, KernelContext_0 thread* kernelContext_2)
{

#line 62
    float4 d_0 = kernelContext_2->tile_0->destinationBounds_0;

#line 70
    if(((kernelContext_2->tile_0->reserved_0) & 1U) != 0U)
    {
        return float2(d_0.x + float(_S8.x) + 0.5f, d_0.y + float(_S8.y) + 0.5f);
    }
    return float2(d_0.x + float(_S8.x) + 0.5f, d_0.w - float(_S8.y) - 0.5f);
}


#line 74
float2 gripTexCoord_0(uint _S9, float2 _S10, KernelContext_0 thread* kernelContext_3)
{

#line 81
    if(_S9 >= 16U)
    {
        return float2(0.0f) ;
    }

    return float2(dot(kernelContext_3->tile_0->textures_0[_S9].u_0.xy, _S10) + kernelContext_3->tile_0->textures_0[_S9].u_0.z, dot(kernelContext_3->tile_0->textures_0[_S9].v_0.xy, _S10) + kernelContext_3->tile_0->textures_0[_S9].v_0.z);
}


#line 117 "GripColorConvert.slang"
[[kernel]] void gripColorConvert(uint3 tid_0 [[thread_position_in_grid]], GripTile_0 constant* tile_1 [[buffer(1)]], texture2d<float, access::sample> source_1 [[texture(0)]], sampler bilinear_1 [[sampler(0)]], GripColorUniforms_0 constant* uniforms_1 [[buffer(0)]], texture2d<float, access::read_write> destination_1 [[texture(1)]])
{

#line 117
    thread KernelContext_0 kernelContext_4;

#line 117
    (&kernelContext_4)->tile_0 = tile_1;

#line 117
    (&kernelContext_4)->source_0 = source_1;

#line 117
    (&kernelContext_4)->bilinear_0 = bilinear_1;

#line 117
    (&kernelContext_4)->uniforms_0 = uniforms_1;

#line 117
    (&kernelContext_4)->destination_0 = destination_1;

    uint2 _S11 = tid_0.xy;

#line 119
    bool _S12 = gripInsideTile_0(_S11, &kernelContext_4);

#line 119
    if(!_S12)
    {
        return;
    }

#line 121
    float2 _S13 = gripImagePixel_0(_S11, &kernelContext_4);

#line 121
    float2 _S14 = gripTexCoord_0(0U, _S13, &kernelContext_4);


    float4 color_0 = (((&kernelContext_4)->source_0).sample(((&kernelContext_4)->bilinear_0), (_S14), level((0.0f))));

#line 124
    bool curved_0;


    if(((&kernelContext_4)->uniforms_0->decode_0) != int(0))
    {

#line 127
        curved_0 = true;

#line 127
    }
    else
    {

#line 127
        curved_0 = ((&kernelContext_4)->uniforms_0->encode_0) != int(0);

#line 127
    }
    if(curved_0)
    {

#line 128
        curved_0 = ((&kernelContext_4)->uniforms_0->premultiplied_0) != int(0);

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
    float3 rgb_0;
    if(unpremultiply_0)
    {

#line 129
        rgb_0 = color_0.xyz / float3(color_0.w) ;

#line 129
    }
    else
    {

#line 129
        rgb_0 = color_0.xyz;

#line 129
    }
    float3 rgb_1 = float3(gripDecode_0((&kernelContext_4)->uniforms_0->decode_0, rgb_0.x), gripDecode_0((&kernelContext_4)->uniforms_0->decode_0, rgb_0.y), gripDecode_0((&kernelContext_4)->uniforms_0->decode_0, rgb_0.z));

    float3 rgb_2 = float3(gripEncode_0((&kernelContext_4)->uniforms_0->encode_0, dot((&kernelContext_4)->uniforms_0->row0_0, rgb_1)), gripEncode_0((&kernelContext_4)->uniforms_0->encode_0, dot((&kernelContext_4)->uniforms_0->row1_0, rgb_1)), gripEncode_0((&kernelContext_4)->uniforms_0->encode_0, dot((&kernelContext_4)->uniforms_0->row2_0, rgb_1)));
    if(unpremultiply_0)
    {

#line 133
        rgb_0 = rgb_2 * float3(color_0.w) ;

#line 133
    }
    else
    {

#line 133
        rgb_0 = rgb_2;

#line 133
    }



    (&kernelContext_4)->destination_0.write(float4(rgb_0, color_0.w),_S11);
    return;
}

