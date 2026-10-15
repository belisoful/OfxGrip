#include "slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif


#line 24 "GripInspect.slang"
struct InspectValues_0
{
    float radius_0;
    float gain_0;
    Vector<float, 2>  center_0;
    bool invert_0;
    uint32_t count_0;
    half level_0;
    int16_t small_0;
    uint8_t byte_0;
    float unbound_0;
};


#line 29 "../../../kernels/GripKernel.slang"
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


#line 48 "GripInspect.slang"
struct GlobalParams_0
{
    InspectValues_0* values_0;
    GripTile_0* tile_0;
    Texture2D<Vector<float, 4> > first_0;
    Texture2D<Vector<float, 4> > second_0;
    Texture2D<Vector<float, 4> > ramp_0;
    Texture2D<Vector<float, 4> > mask_0;
    RWTexture2D<Vector<float, 4> > destination_0;
    SamplerState bilinear_0;
    int32_t MODE_0;
    int32_t TAPS_0;
    float FILL_0;
};


#line 48
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 10374 "hlsl.meta.slang"
static float dot_0(Vector<float, 2>  x_0, Vector<float, 2>  y_0)
{

#line 10401
    return x_0.x * y_0.x + x_0.y * y_0.y;
}


#line 10401
static Vector<uint32_t, 2>  gripTileSize_0(KernelContext_0 * kernelContext_0)
{

#line 54 "../../../kernels/GripKernel.slang"
    return Vector<uint32_t, 2> (uint32_t(kernelContext_0->globalParams_0->tile_0->destinationBounds_0.z - kernelContext_0->globalParams_0->tile_0->destinationBounds_0.x), uint32_t(kernelContext_0->globalParams_0->tile_0->destinationBounds_0.w - kernelContext_0->globalParams_0->tile_0->destinationBounds_0.y));
}


#line 54
static bool gripInsideTile_0(Vector<uint32_t, 2>  _S1, KernelContext_0 * kernelContext_1)
{

#line 54
    Vector<uint32_t, 2>  _S2 = gripTileSize_0(kernelContext_1);

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
static Vector<float, 2>  gripImagePixel_0(Vector<uint32_t, 2>  _S4, KernelContext_0 * kernelContext_2)
{

#line 62
    Vector<float, 4>  d_0 = kernelContext_2->globalParams_0->tile_0->destinationBounds_0;

#line 70
    if(((kernelContext_2->globalParams_0->tile_0->reserved_0) & 1U) != 0U)
    {
        return Vector<float, 2> (d_0.x + float(_S4.x) + 0.5f, d_0.y + float(_S4.y) + 0.5f);
    }
    return Vector<float, 2> (d_0.x + float(_S4.x) + 0.5f, d_0.w - float(_S4.y) - 0.5f);
}


#line 74
static Vector<float, 2>  gripTexCoord_0(uint32_t _S5, Vector<float, 2>  _S6, KernelContext_0 * kernelContext_3)
{

#line 81
    if(_S5 >= 16U)
    {
        return (Vector<float, 2> )0.0f;
    }

#line 83
    Vector<float, 4>  _S7 = kernelContext_3->globalParams_0->tile_0->textures_0[_S5].u_0;

#line 83
    Vector<float, 4>  _S8 = kernelContext_3->globalParams_0->tile_0->textures_0[_S5].v_0;


    return Vector<float, 2> (dot_0(Vector<float, 2> {_S7.x, _S7.y}, _S6) + kernelContext_3->globalParams_0->tile_0->textures_0[_S5].u_0.z, dot_0(Vector<float, 2> {_S8.x, _S8.y}, _S6) + kernelContext_3->globalParams_0->tile_0->textures_0[_S5].v_0.z);
}


#line 54 "GripInspect.slang"
void _fxGripKernel(void* _S9, void* entryPointParams_0, void* globalParams_1)
{

#line 54
    ComputeThreadVaryingInput * _S10 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S9));

#line 54
    KernelContext_0 kernelContext_4;

#line 54
    (&kernelContext_4)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));

#line 54
    Vector<uint32_t, 3>  _S11 = _S10->groupID * Vector<uint32_t, 3> (8U, 8U, 1U) + _S10->groupThreadID;

    Vector<uint32_t, 2>  _S12 = Vector<uint32_t, 2> {_S11.x, _S11.y};

#line 56
    bool _S13 = gripInsideTile_0(_S12, &kernelContext_4);

#line 56
    if(!_S13)
    {
        return;
    }

#line 58
    Vector<float, 2>  _S14 = gripImagePixel_0(_S12, &kernelContext_4);

#line 58
    Vector<float, 4>  result_0;



    switch((&kernelContext_4)->globalParams_0->MODE_0)
    {
    case int(1):
        {

#line 62
            result_0 = Vector<float, 4> (_S14.x, _S14.y, (slang_bit_cast<GlobalParams_0*>(globalParams_1))->tile_0->pixelScale_0.x, (slang_bit_cast<GlobalParams_0*>(globalParams_1))->tile_0->pixelScale_0.y);



            break;
        }
    case int(2):
        {

#line 66
            Vector<float, 2>  _S15 = gripTexCoord_0(0U, _S14, &kernelContext_4);

#line 66
            Vector<float, 2>  _S16 = gripTexCoord_0(1U, _S14, &kernelContext_4);

#line 66
            result_0 = Vector<float, 4> (_S15.x, _S15.y, _S16.x, _S16.y);


            break;
        }
    case int(3):
        {

#line 69
            Vector<float, 2>  _S17 = gripTexCoord_0(0U, _S14, &kernelContext_4);

            Vector<float, 4>  _S18 = (&kernelContext_4)->globalParams_0->first_0.SampleLevel((&kernelContext_4)->globalParams_0->bilinear_0, _S17, 0.0f);

#line 71
            Vector<float, 2>  _S19 = gripTexCoord_0(1U, _S14, &kernelContext_4);

#line 71
            result_0 = _S18 - (&kernelContext_4)->globalParams_0->second_0.SampleLevel((&kernelContext_4)->globalParams_0->bilinear_0, _S19, 0.0f);

            break;
        }
    case int(4):
        {

#line 73
            result_0 = Vector<float, 4> ((&kernelContext_4)->globalParams_0->values_0->center_0.x, (&kernelContext_4)->globalParams_0->values_0->center_0.y, float((&kernelContext_4)->globalParams_0->values_0->invert_0), float((&kernelContext_4)->globalParams_0->values_0->count_0));


            break;
        }
    case int(5):
        {

#line 76
            result_0 = Vector<float, 4> (float((&kernelContext_4)->globalParams_0->values_0->level_0), float((&kernelContext_4)->globalParams_0->values_0->small_0), float((&kernelContext_4)->globalParams_0->values_0->byte_0), (&kernelContext_4)->globalParams_0->values_0->unbound_0);


            break;
        }
    case int(6):
        {

#line 79
            Vector<float, 2>  _S20 = gripTexCoord_0(3U, _S14, &kernelContext_4);

            Vector<float, 4>  _S21 = (&kernelContext_4)->globalParams_0->mask_0.SampleLevel((&kernelContext_4)->globalParams_0->bilinear_0, _S20, 0.0f);

#line 81
            result_0 = Vector<float, 4> (Vector<float, 2> {_S21.x, _S21.y}.x, Vector<float, 2> {_S21.x, _S21.y}.y, (&kernelContext_4)->globalParams_0->ramp_0.Load(Vector<int32_t, 3> (int(2), int(0), int(0))).z, float((slang_bit_cast<GlobalParams_0*>(globalParams_1))->tile_0->textureCount_0));

            break;
        }
    case int(7):
        {

#line 83
            Vector<float, 2>  _S22 = gripTexCoord_0(0U, _S14, &kernelContext_4);


            Vector<float, 4>  color_0 = (&kernelContext_4)->globalParams_0->first_0.SampleLevel((&kernelContext_4)->globalParams_0->bilinear_0, _S22, 0.0f);

#line 86
            result_0 = Vector<float, 4> (color_0.x, 0.0f, 0.0f, color_0.w);

            break;
        }
    case int(8):
        {

#line 88
            result_0 = (Vector<float, 4> )(&kernelContext_4)->globalParams_0->FILL_0;



            break;
        }
    default:
        {

#line 92
            result_0 = Vector<float, 4> ((&kernelContext_4)->globalParams_0->values_0->radius_0, (&kernelContext_4)->globalParams_0->values_0->gain_0, float((&kernelContext_4)->globalParams_0->TAPS_0), 1.0f);


            break;
        }
    }

#line 97
    (&kernelContext_4)->globalParams_0->destination_0[_S12] = result_0;
    return;
}

// [numthreads(8, 8, 1)]
SLANG_PRELUDE_EXPORT
void fxGripKernel_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _fxGripKernel(varyingInput, entryPointParams, globalParams);
}
// [numthreads(8, 8, 1)]
SLANG_PRELUDE_EXPORT
void fxGripKernel_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    for (uint32_t y = 0; y < 8; ++y)
    {
        threadInput.groupThreadID.y = y;
        for (uint32_t x = 0; x < 8; ++x)
        {
            threadInput.groupThreadID.x = x;
            _fxGripKernel(&threadInput, entryPointParams, globalParams);
        }
    }
}
// [numthreads(8, 8, 1)]
SLANG_PRELUDE_EXPORT
void fxGripKernel(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
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
                fxGripKernel_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
