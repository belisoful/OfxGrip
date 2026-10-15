#include "slang-cpp-prelude.h"

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif


#line 28 "Spot.slang"
struct SpotUniforms_0
{
    Vector<float, 2>  center_0;
    float radius_0;
    float softness_0;
    float outsideGain_0;
    Vector<float, 3>  tint_0;
};


#line 29 "../../kernels/GripKernel.slang"
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


#line 43 "Spot.slang"
struct GlobalParams_0
{
    SpotUniforms_0* uniforms_0;
    GripTile_0* tile_0;
    Texture2D<Vector<float, 4> > source_0;
    RWTexture2D<Vector<float, 4> > destination_0;
    SamplerState bilinear_0;
    bool INVERT_0;
};


#line 43
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 9341 "hlsl.meta.slang"
static float clamp_0(float x_0, float minBound_0, float maxBound_0)
{

#line 9353
    return (F32_min(((F32_max((x_0), (minBound_0)))), (maxBound_0)));
}


#line 14946
static float saturate_0(float x_1)
{

#line 14954
    return clamp_0(x_1, 0.0f, 1.0f);
}


#line 12691
static float lerp_0(float x_2, float y_0, float s_0)
{

#line 12703
    return x_2 + (y_0 - x_2) * s_0;
}


#line 15362
static float smoothstep_0(float min_0, float max_0, float x_3)
{

#line 15374
    float _S1 = saturate_0((x_3 - min_0) / (max_0 - min_0));
    return _S1 * _S1 * (3.0f - (_S1 + _S1));
}


#line 10374
static float dot_0(Vector<float, 2>  x_4, Vector<float, 2>  y_1)
{

#line 10401
    return x_4.x * y_1.x + x_4.y * y_1.y;
}


#line 12651
static float length_0(Vector<float, 2>  x_5)
{

#line 12663
    return (F32_sqrt((dot_0(x_5, x_5))));
}


#line 3785
static Vector<uint32_t, 2>  gripTileSize_0(KernelContext_0 * kernelContext_0)
{

#line 54 "../../kernels/GripKernel.slang"
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


    return Vector<float, 2> (dot_0(Vector<float, 2> {_S8.x, _S8.y}, _S7) + kernelContext_3->globalParams_0->tile_0->textures_0[_S6].u_0.z, dot_0(Vector<float, 2> {_S9.x, _S9.y}, _S7) + kernelContext_3->globalParams_0->tile_0->textures_0[_S6].v_0.z);
}


#line 47 "Spot.slang"
void _spotKernel(void* _S10, void* entryPointParams_0, void* globalParams_1)
{

#line 47
    ComputeThreadVaryingInput * _S11 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S10));

#line 47
    KernelContext_0 kernelContext_4;

#line 47
    (&kernelContext_4)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));

#line 47
    Vector<uint32_t, 3>  _S12 = _S11->groupID * Vector<uint32_t, 3> (8U, 8U, 1U) + _S11->groupThreadID;

    Vector<uint32_t, 2>  _S13 = Vector<uint32_t, 2> {_S12.x, _S12.y};

#line 49
    bool _S14 = gripInsideTile_0(_S13, &kernelContext_4);

#line 49
    if(!_S14)
    {
        return;
    }

#line 51
    Vector<float, 2>  _S15 = gripImagePixel_0(_S13, &kernelContext_4);

#line 51
    Vector<float, 2>  _S16 = gripTexCoord_0(0U, _S15, &kernelContext_4);


    Vector<float, 4>  color_0 = (&kernelContext_4)->globalParams_0->source_0.SampleLevel((&kernelContext_4)->globalParams_0->bilinear_0, _S16, 0.0f);

    float outside_0 = smoothstep_0((&kernelContext_4)->globalParams_0->uniforms_0->radius_0, (&kernelContext_4)->globalParams_0->uniforms_0->radius_0 + (F32_max(((&kernelContext_4)->globalParams_0->uniforms_0->softness_0), (0.00009999999747379f))), length_0(_S15 - (&kernelContext_4)->globalParams_0->uniforms_0->center_0));

#line 56
    float outside_1;
    if((&kernelContext_4)->globalParams_0->INVERT_0)
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

#line 62
    (&kernelContext_4)->globalParams_0->destination_0[_S13] = Vector<float, 4> ((Vector<float, 3> {color_0.x, color_0.y, color_0.z} * (Vector<float, 3> )lerp_0(1.0f, (&kernelContext_4)->globalParams_0->uniforms_0->outsideGain_0, outside_1) * (&kernelContext_4)->globalParams_0->uniforms_0->tint_0).x, (Vector<float, 3> {color_0.x, color_0.y, color_0.z} * (Vector<float, 3> )lerp_0(1.0f, (&kernelContext_4)->globalParams_0->uniforms_0->outsideGain_0, outside_1) * (&kernelContext_4)->globalParams_0->uniforms_0->tint_0).y, (Vector<float, 3> {color_0.x, color_0.y, color_0.z} * (Vector<float, 3> )lerp_0(1.0f, (&kernelContext_4)->globalParams_0->uniforms_0->outsideGain_0, outside_1) * (&kernelContext_4)->globalParams_0->uniforms_0->tint_0).z, color_0.w);
    return;
}

// [numthreads(8, 8, 1)]
SLANG_PRELUDE_EXPORT
void spotKernel_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _spotKernel(varyingInput, entryPointParams, globalParams);
}
// [numthreads(8, 8, 1)]
SLANG_PRELUDE_EXPORT
void spotKernel_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    for (uint32_t y = 0; y < 8; ++y)
    {
        threadInput.groupThreadID.y = y;
        for (uint32_t x = 0; x < 8; ++x)
        {
            threadInput.groupThreadID.x = x;
            _spotKernel(&threadInput, entryPointParams, globalParams);
        }
    }
}
// [numthreads(8, 8, 1)]
SLANG_PRELUDE_EXPORT
void spotKernel(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
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
                spotKernel_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
