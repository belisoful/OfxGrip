#include "slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif


#line 24 "GripColorConvert.slang"
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


#line 49 "GripColorConvert.slang"
static float gripDecodeMagnitude_0(int32_t transfer_0, float e_0)
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
                _S1 = (F32_pow(((e_0 + 0.05499999970197678f) / 1.0549999475479126f), (2.40000009536743164f)));

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
                _S1 = (F32_pow(((e_0 + 0.0989999994635582f) / 1.09899997711181641f), (2.22222232818603516f)));

#line 54
            }

#line 54
            return _S1;
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



static float gripDecode_0(int32_t transfer_1, float encoded_0)
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
    float _S2;

    if(transfer_1 == int(8))
    {
        float e_1 = clamp_0(encoded_0, 0.0f, 1.0f);
        if(e_1 <= 0.5f)
        {

#line 77
            _S2 = e_1 * e_1 / 3.0f;

#line 77
        }
        else
        {

#line 77
            _S2 = ((F32_exp(((e_1 - 0.55991071462631226f) / 0.1788327693939209f))) + 0.28466892242431641f) / 12.0f;

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

static float gripEncodeMagnitude_0(int32_t transfer_2, float l_0)
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
                _S3 = 1.0549999475479126f * (F32_pow((l_0), (0.4166666567325592f))) - 0.05499999970197678f;

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
                _S3 = 1.09899997711181641f * (F32_pow((l_0), (0.44999998807907104f))) - 0.0989999994635582f;

#line 87
            }

#line 87
            return _S3;
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



static float gripEncode_0(int32_t transfer_3, float linear_0)
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
    float _S4;

    if(transfer_3 == int(8))
    {
        float l_1 = clamp_0(linear_0, 0.0f, 1.0f);
        if(l_1 <= 0.0833333358168602f)
        {

#line 110
            _S4 = (F32_sqrt((3.0f * l_1)));

#line 110
        }
        else
        {

#line 110
            _S4 = 0.1788327693939209f * (F32_log((12.0f * l_1 - 0.28466892242431641f))) + 0.55991071462631226f;

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


#line 112
static Vector<uint32_t, 2>  gripTileSize_0(KernelContext_0 * kernelContext_0)
{

#line 54 "GripKernel.slang"
    return Vector<uint32_t, 2> (uint32_t(kernelContext_0->globalParams_0->tile_0->destinationBounds_0.z - kernelContext_0->globalParams_0->tile_0->destinationBounds_0.x), uint32_t(kernelContext_0->globalParams_0->tile_0->destinationBounds_0.w - kernelContext_0->globalParams_0->tile_0->destinationBounds_0.y));
}


#line 54
static bool gripInsideTile_0(Vector<uint32_t, 2>  _S5, KernelContext_0 * kernelContext_1)
{

#line 54
    Vector<uint32_t, 2>  _S6 = gripTileSize_0(kernelContext_1);

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
static Vector<float, 2>  gripImagePixel_0(Vector<uint32_t, 2>  _S8, KernelContext_0 * kernelContext_2)
{

#line 62
    Vector<float, 4>  d_0 = kernelContext_2->globalParams_0->tile_0->destinationBounds_0;

#line 70
    if(((kernelContext_2->globalParams_0->tile_0->reserved_0) & 1U) != 0U)
    {
        return Vector<float, 2> (d_0.x + float(_S8.x) + 0.5f, d_0.y + float(_S8.y) + 0.5f);
    }
    return Vector<float, 2> (d_0.x + float(_S8.x) + 0.5f, d_0.w - float(_S8.y) - 0.5f);
}


#line 74
static Vector<float, 2>  gripTexCoord_0(uint32_t _S9, Vector<float, 2>  _S10, KernelContext_0 * kernelContext_3)
{

#line 81
    if(_S9 >= 16U)
    {
        return (Vector<float, 2> )0.0f;
    }

#line 83
    Vector<float, 4>  _S11 = kernelContext_3->globalParams_0->tile_0->textures_0[_S9].u_0;

#line 83
    Vector<float, 4>  _S12 = kernelContext_3->globalParams_0->tile_0->textures_0[_S9].v_0;


    return Vector<float, 2> (dot_1(Vector<float, 2> {_S11.x, _S11.y}, _S10) + kernelContext_3->globalParams_0->tile_0->textures_0[_S9].u_0.z, dot_1(Vector<float, 2> {_S12.x, _S12.y}, _S10) + kernelContext_3->globalParams_0->tile_0->textures_0[_S9].v_0.z);
}


#line 117 "GripColorConvert.slang"
void _gripColorConvert(void* _S13, void* entryPointParams_0, void* globalParams_1)
{

#line 117
    ComputeThreadVaryingInput * _S14 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S13));

#line 117
    KernelContext_0 kernelContext_4;

#line 117
    (&kernelContext_4)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));

#line 117
    Vector<uint32_t, 3>  _S15 = _S14->groupID * Vector<uint32_t, 3> (8U, 8U, 1U) + _S14->groupThreadID;

    Vector<uint32_t, 2>  _S16 = Vector<uint32_t, 2> {_S15.x, _S15.y};

#line 119
    bool _S17 = gripInsideTile_0(_S16, &kernelContext_4);

#line 119
    if(!_S17)
    {
        return;
    }

#line 121
    Vector<float, 2>  _S18 = gripImagePixel_0(_S16, &kernelContext_4);

#line 121
    Vector<float, 2>  _S19 = gripTexCoord_0(0U, _S18, &kernelContext_4);


    Vector<float, 4>  color_0 = (&kernelContext_4)->globalParams_0->source_0.SampleLevel((&kernelContext_4)->globalParams_0->bilinear_0, _S19, 0.0f);

#line 124
    bool curved_0;


    if(((&kernelContext_4)->globalParams_0->uniforms_0->decode_0) != int(0))
    {

#line 127
        curved_0 = true;

#line 127
    }
    else
    {

#line 127
        curved_0 = ((&kernelContext_4)->globalParams_0->uniforms_0->encode_0) != int(0);

#line 127
    }
    if(curved_0)
    {

#line 128
        curved_0 = ((&kernelContext_4)->globalParams_0->uniforms_0->premultiplied_0) != int(0);

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
    Vector<float, 3>  rgb_0;
    if(unpremultiply_0)
    {

#line 129
        rgb_0 = Vector<float, 3> {color_0.x, color_0.y, color_0.z} / (Vector<float, 3> )color_0.w;

#line 129
    }
    else
    {

#line 129
        rgb_0 = Vector<float, 3> {color_0.x, color_0.y, color_0.z};

#line 129
    }
    Vector<float, 3>  rgb_1 = Vector<float, 3> (gripDecode_0((&kernelContext_4)->globalParams_0->uniforms_0->decode_0, rgb_0.x), gripDecode_0((&kernelContext_4)->globalParams_0->uniforms_0->decode_0, rgb_0.y), gripDecode_0((&kernelContext_4)->globalParams_0->uniforms_0->decode_0, rgb_0.z));

    Vector<float, 3>  rgb_2 = Vector<float, 3> (gripEncode_0((&kernelContext_4)->globalParams_0->uniforms_0->encode_0, dot_0((&kernelContext_4)->globalParams_0->uniforms_0->row0_0, rgb_1)), gripEncode_0((&kernelContext_4)->globalParams_0->uniforms_0->encode_0, dot_0((&kernelContext_4)->globalParams_0->uniforms_0->row1_0, rgb_1)), gripEncode_0((&kernelContext_4)->globalParams_0->uniforms_0->encode_0, dot_0((&kernelContext_4)->globalParams_0->uniforms_0->row2_0, rgb_1)));
    if(unpremultiply_0)
    {

#line 133
        rgb_0 = rgb_2 * (Vector<float, 3> )color_0.w;

#line 133
    }
    else
    {

#line 133
        rgb_0 = rgb_2;

#line 133
    }



    (&kernelContext_4)->globalParams_0->destination_0[_S16] = Vector<float, 4> (rgb_0.x, rgb_0.y, rgb_0.z, color_0.w);
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
