#include "slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif


#line 60 "GripColorConvert.slang"
struct GripCurve_0
{
    float threshold_0;
    float slope_0;
    float offset_0;
    float scale_0;
    float exponent_0;
};


#line 24
struct GripColorUniforms_0
{
    Vector<float, 3>  row0_0;
    Vector<float, 3>  row1_0;
    Vector<float, 3>  row2_0;
    int32_t decode_0;
    int32_t encode_0;
    int32_t premultiplied_0;
};


#line 29 "GripKernel.slang"
struct GripTileMap_0
{
    Vector<float, 4>  u_0;
    Vector<float, 4>  v_0;
};


#line 41
struct GripTile_0
{
    Vector<float, 4>  destinationBounds_0;
    Vector<float, 2>  pixelScale_0;
    uint32_t textureCount_0;
    uint32_t reserved_0;
    FixedArray<GripTileMap_0, 16>  textures_0;
};


#line 41
struct GlobalParams_0
{
    GripColorUniforms_0* uniforms_0;
    GripTile_0* tile_0;
    Texture2D<Vector<float, 4> > source_0;
    RWTexture2D<Vector<float, 4> > destination_0;
    SamplerState bilinear_0;
};


#line 41
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 10374 "hlsl.meta.slang"
static float dot_0(Vector<float, 3>  x_0, Vector<float, 3>  y_0)
{

#line 10401
    return x_0.x * y_0.x + x_0.y * y_0.y + x_0.z * y_0.z;
}


#line 9341
static float clamp_0(float x_1, float minBound_0, float maxBound_0)
{

#line 9353
    return (F32_min(((F32_max((x_1), (minBound_0)))), (maxBound_0)));
}


#line 10374
static float dot_1(Vector<float, 2>  x_2, Vector<float, 2>  y_1)
{

#line 10401
    return x_2.x * y_1.x + x_2.y * y_1.y;
}


#line 69 "GripColorConvert.slang"
static GripCurve_0 gripDecodeCurve_0(int32_t transfer_0)
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

static GripCurve_0 gripEncodeCurve_0(int32_t transfer_1)
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
static float gripPow_0(float base_0, float exponent_1)
{

#line 51
    float _S1;

    if(base_0 > 0.0f)
    {

#line 53
        _S1 = (F32_exp((exponent_1 * (F32_log((base_0))))));

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
static float gripDecode_0(int32_t transfer_2, GripCurve_0 * curve_2, float encoded_0)
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

static float gripEncode_0(int32_t transfer_3, GripCurve_0 * curve_3, float linear_1)
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


#line 150
static Vector<uint32_t, 2>  gripTileSize_0(KernelContext_0 * kernelContext_0)
{

#line 54 "GripKernel.slang"
    return Vector<uint32_t, 2> (uint32_t(kernelContext_0->globalParams_0->tile_0->destinationBounds_0.z - kernelContext_0->globalParams_0->tile_0->destinationBounds_0.x), uint32_t(kernelContext_0->globalParams_0->tile_0->destinationBounds_0.w - kernelContext_0->globalParams_0->tile_0->destinationBounds_0.y));
}


#line 54
static bool gripInsideTile_0(Vector<uint32_t, 2>  _S2, KernelContext_0 * kernelContext_1)
{

#line 54
    Vector<uint32_t, 2>  _S3 = gripTileSize_0(kernelContext_1);

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
static Vector<float, 2>  gripImagePixel_0(Vector<uint32_t, 2>  _S5, KernelContext_0 * kernelContext_2)
{

#line 62
    Vector<float, 4>  d_0 = kernelContext_2->globalParams_0->tile_0->destinationBounds_0;

#line 70
    if(((kernelContext_2->globalParams_0->tile_0->reserved_0) & 1U) != 0U)
    {
        return Vector<float, 2> (d_0.x + float(_S5.x) + 0.5f, d_0.y + float(_S5.y) + 0.5f);
    }
    return Vector<float, 2> (d_0.x + float(_S5.x) + 0.5f, d_0.w - float(_S5.y) - 0.5f);
}


#line 74
static Vector<float, 2>  gripTexCoord_0(uint32_t _S6, Vector<float, 2>  _S7, KernelContext_0 * kernelContext_3)
{

#line 81
    if(_S6 >= 16U)
    {
        return (Vector<float, 2> )0.0f;
    }

#line 83
    Vector<float, 4>  _S8 = kernelContext_3->globalParams_0->tile_0->textures_0[_S6].u_0;

#line 83
    Vector<float, 4>  _S9 = kernelContext_3->globalParams_0->tile_0->textures_0[_S6].v_0;


    return Vector<float, 2> (dot_1(Vector<float, 2> {_S8.x, _S8.y}, _S7) + kernelContext_3->globalParams_0->tile_0->textures_0[_S6].u_0.z, dot_1(Vector<float, 2> {_S9.x, _S9.y}, _S7) + kernelContext_3->globalParams_0->tile_0->textures_0[_S6].v_0.z);
}


#line 155 "GripColorConvert.slang"
void _gripColorConvert(void* _S10, void* entryPointParams_0, void* globalParams_1)
{

#line 155
    ComputeThreadVaryingInput * _S11 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S10));

#line 155
    KernelContext_0 kernelContext_4;

#line 155
    (&kernelContext_4)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));

#line 155
    Vector<uint32_t, 3>  _S12 = _S11->groupID * Vector<uint32_t, 3> (8U, 8U, 1U) + _S11->groupThreadID;

    Vector<uint32_t, 2>  _S13 = Vector<uint32_t, 2> {_S12.x, _S12.y};

#line 157
    bool _S14 = gripInsideTile_0(_S13, &kernelContext_4);

#line 157
    if(!_S14)
    {
        return;
    }

#line 159
    Vector<float, 2>  _S15 = gripImagePixel_0(_S13, &kernelContext_4);

#line 159
    Vector<float, 2>  _S16 = gripTexCoord_0(0U, _S15, &kernelContext_4);


    Vector<float, 4>  color_0 = (&kernelContext_4)->globalParams_0->source_0.SampleLevel((&kernelContext_4)->globalParams_0->bilinear_0, _S16, 0.0f);

#line 162
    bool curved_0;


    if(((&kernelContext_4)->globalParams_0->uniforms_0->decode_0) != int(0))
    {

#line 165
        curved_0 = true;

#line 165
    }
    else
    {

#line 165
        curved_0 = ((&kernelContext_4)->globalParams_0->uniforms_0->encode_0) != int(0);

#line 165
    }
    if(curved_0)
    {

#line 166
        curved_0 = ((&kernelContext_4)->globalParams_0->uniforms_0->premultiplied_0) != int(0);

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
    Vector<float, 3>  rgb_0;
    if(unpremultiply_0)
    {

#line 167
        rgb_0 = Vector<float, 3> {color_0.x, color_0.y, color_0.z} / (Vector<float, 3> )color_0.w;

#line 167
    }
    else
    {

#line 167
        rgb_0 = Vector<float, 3> {color_0.x, color_0.y, color_0.z};

#line 167
    }
    GripCurve_0 decode_1 = gripDecodeCurve_0((&kernelContext_4)->globalParams_0->uniforms_0->decode_0);
    GripCurve_0 encode_1 = gripEncodeCurve_0((&kernelContext_4)->globalParams_0->uniforms_0->encode_0);
    int32_t _S17 = (&kernelContext_4)->globalParams_0->uniforms_0->decode_0;

#line 170
    float _S18 = rgb_0.x;

#line 170
    GripCurve_0 _S19 = decode_1;

#line 170
    float _S20 = gripDecode_0(_S17, &_S19, _S18);

#line 170
    int32_t _S21 = (&kernelContext_4)->globalParams_0->uniforms_0->decode_0;

#line 170
    float _S22 = rgb_0.y;

#line 170
    GripCurve_0 _S23 = decode_1;

#line 170
    float _S24 = gripDecode_0(_S21, &_S23, _S22);

#line 170
    int32_t _S25 = (&kernelContext_4)->globalParams_0->uniforms_0->decode_0;

#line 170
    float _S26 = rgb_0.z;

#line 170
    GripCurve_0 _S27 = decode_1;

#line 170
    float _S28 = gripDecode_0(_S25, &_S27, _S26);

#line 170
    Vector<float, 3>  rgb_1 = Vector<float, 3> (_S20, _S24, _S28);
    float _S29 = dot_0((&kernelContext_4)->globalParams_0->uniforms_0->row0_0, rgb_1);

#line 171
    float _S30 = dot_0((&kernelContext_4)->globalParams_0->uniforms_0->row1_0, rgb_1);

#line 171
    float _S31 = dot_0((&kernelContext_4)->globalParams_0->uniforms_0->row2_0, rgb_1);
    int32_t _S32 = (&kernelContext_4)->globalParams_0->uniforms_0->encode_0;

#line 172
    GripCurve_0 _S33 = encode_1;

#line 172
    float _S34 = gripEncode_0(_S32, &_S33, _S29);

#line 172
    int32_t _S35 = (&kernelContext_4)->globalParams_0->uniforms_0->encode_0;

#line 172
    GripCurve_0 _S36 = encode_1;

#line 172
    float _S37 = gripEncode_0(_S35, &_S36, _S30);

#line 172
    int32_t _S38 = (&kernelContext_4)->globalParams_0->uniforms_0->encode_0;

#line 172
    GripCurve_0 _S39 = encode_1;

#line 172
    float _S40 = gripEncode_0(_S38, &_S39, _S31);

#line 172
    Vector<float, 3>  rgb_2 = Vector<float, 3> (_S34, _S37, _S40);
    if(unpremultiply_0)
    {

#line 173
        rgb_0 = rgb_2 * (Vector<float, 3> )color_0.w;

#line 173
    }
    else
    {

#line 173
        rgb_0 = rgb_2;

#line 173
    }



    (&kernelContext_4)->globalParams_0->destination_0[_S13] = Vector<float, 4> (rgb_0.x, rgb_0.y, rgb_0.z, color_0.w);
    return;
}

// [numthreads(8, 8, 1)]
SLANG_PRELUDE_EXPORT
void gripColorConvert_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _gripColorConvert(varyingInput, entryPointParams, globalParams);
}
// [numthreads(8, 8, 1)]
SLANG_PRELUDE_EXPORT
void gripColorConvert_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    for (uint32_t y = 0; y < 8; ++y)
    {
        threadInput.groupThreadID.y = y;
        for (uint32_t x = 0; x < 8; ++x)
        {
            threadInput.groupThreadID.x = x;
            _gripColorConvert(&threadInput, entryPointParams, globalParams);
        }
    }
}
// [numthreads(8, 8, 1)]
SLANG_PRELUDE_EXPORT
void gripColorConvert(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeVaryingInput vi = *varyingInput;
    ComputeVaryingInput groupVaryingInput = {};
    for (uint32_t z = vi.startGroupID.z; z < vi.endGroupID.z; ++z)
    {
        groupVaryingInput.startGroupID.z = z;
        for (uint32_t y = vi.startGroupID.y; y < vi.endGroupID.y; ++y)
        {
            groupVaryingInput.startGroupID.y = y;
            for (uint32_t x = vi.startGroupID.x; x < vi.endGroupID.x; ++x)
            {
                groupVaryingInput.startGroupID.x = x;
                gripColorConvert_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
