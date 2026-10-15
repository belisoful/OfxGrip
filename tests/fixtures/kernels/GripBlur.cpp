#include "slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif


#line 21 "GripBlur.slang"
struct BlurUniforms_0
{
    float radius_0;
    Vector<float, 3>  tint_0;
    Vector<int32_t, 2>  offset_0;
    bool enabled_0;
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


#line 55 "GripBlur.slang"
struct GlobalParams_0
{
    BlurUniforms_0* uniforms_0;
    GripTile_0* tile_0;
    Texture2D<Vector<float, 4> > source_0;
    Texture2D<Vector<float, 4> > matte_0;
    RWTexture2D<Vector<float, 4> > destination_0;
    SamplerState bilinear_0;
    int32_t TAPS_0;
    bool INVERT_0;
};


#line 2840 "core.meta.slang"
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


#line 41 "GripBlur.slang"
void _fxGripKernel(void* _S9, void* entryPointParams_0, void* globalParams_1)
{

#line 41
    GlobalParams_0* _S10;

#line 41
    ComputeThreadVaryingInput * _S11 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S9));

#line 41
    KernelContext_0 kernelContext_4;

#line 41
    (&kernelContext_4)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));

#line 41
    Vector<uint32_t, 3>  _S12 = _S11->groupID * Vector<uint32_t, 3> (8U, 8U, 1U) + _S11->groupThreadID;

    Vector<uint32_t, 2>  _S13 = Vector<uint32_t, 2> {_S12.x, _S12.y};

#line 43
    bool _S14 = gripInsideTile_0(_S13, &kernelContext_4);

#line 43
    if(!_S14)
    {
        return;
    }

#line 45
    Vector<float, 2>  _S15 = gripImagePixel_0(_S13, &kernelContext_4);

#line 45
    Vector<float, 2>  _S16 = gripTexCoord_0(0U, _S15, &kernelContext_4);



    uint32_t width_0;

#line 49
    uint32_t height_0;
    (&kernelContext_4)->globalParams_0->source_0.GetDimensions(&width_0, &height_0);
    Vector<float, 2>  _S17 = Vector<float, 2> ((&kernelContext_4)->globalParams_0->uniforms_0->radius_0 / float(width_0), 0.0f);
    Vector<int32_t, 4>  _S18 = (Vector<int32_t, 4> )int(0);

#line 52
    Vector<float, 4>  _S19 = Vector<float, 4> {(float)_slang_vector_get_element(_S18, 0), (float)_slang_vector_get_element(_S18, 1), (float)_slang_vector_get_element(_S18, 2), (float)_slang_vector_get_element(_S18, 3)};

#line 52
    int32_t tap_0 = int(0);

#line 52
    Vector<float, 4>  sum_0 = _S19;
    for(;;)
    {

#line 53
        _S10 = (&kernelContext_4)->globalParams_0;

#line 53
        if(tap_0 < ((&kernelContext_4)->globalParams_0->TAPS_0))
        {
        }
        else
        {

#line 53
            break;
        }

#line 53
        float position_0;

        if(((&kernelContext_4)->globalParams_0->TAPS_0) > int(1))
        {

#line 55
            position_0 = float(tap_0) / float((&kernelContext_4)->globalParams_0->TAPS_0 - int(1)) * 2.0f - 1.0f;

#line 55
        }
        else
        {

#line 55
            position_0 = 0.0f;

#line 55
        }
        Vector<float, 4>  sum_1 = sum_0 + (&kernelContext_4)->globalParams_0->source_0.SampleLevel((&kernelContext_4)->globalParams_0->bilinear_0, _S16 + _S17 * (Vector<float, 2> )position_0, 0.0f);

#line 53
        tap_0 = tap_0 + int(1);

#line 53
        sum_0 = sum_1;

#line 53
    }

#line 58
    Vector<float, 4>  color_0 = sum_0 / (Vector<float, 4> )float(_S10->TAPS_0);

#line 58
    Vector<float, 2>  _S20 = gripTexCoord_0(1U, _S15, &kernelContext_4);

    Vector<float, 3>  rgb_0 = Vector<float, 3> {color_0.x, color_0.y, color_0.z} * (&kernelContext_4)->globalParams_0->uniforms_0->tint_0 * (Vector<float, 3> )(&kernelContext_4)->globalParams_0->matte_0.SampleLevel((&kernelContext_4)->globalParams_0->bilinear_0, _S20, 0.0f).w;

#line 60
    Vector<float, 3>  rgb_1;
    if((&kernelContext_4)->globalParams_0->INVERT_0)
    {

#line 61
        rgb_1 = (Vector<float, 3> )color_0.w - rgb_0;

#line 61
    }
    else
    {

#line 61
        rgb_1 = rgb_0;

#line 61
    }



    (&kernelContext_4)->globalParams_0->destination_0[_S13] = Vector<float, 4> (rgb_1.x, rgb_1.y, rgb_1.z, color_0.w);
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
