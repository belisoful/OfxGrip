#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 43 "Spot.slang"
constant bool fc_INVERT_0 [[function_constant(0)]];
constant bool INVERT_0 = is_function_constant_defined(fc_INVERT_0) ? fc_INVERT_0 : false;

#line 29 "../../kernels/GripKernel.slang"
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


#line 28 "Spot.slang"
struct SpotUniforms_0
{
    float2 center_0;
    float radius_0;
    float softness_0;
    float outsideGain_0;
    float3 tint_0;
};




struct KernelContext_0
{
    GripTile_0 constant* tile_0;
    texture2d<float, access::sample> source_0;
    sampler bilinear_0;
    SpotUniforms_0 constant* uniforms_0;
    texture2d<float, access::read_write> destination_0;
};


#line 2839 "core.meta.slang"
uint2 gripTileSize_0(KernelContext_0 thread* kernelContext_0)
{

#line 54 "../../kernels/GripKernel.slang"
    return uint2(uint(kernelContext_0->tile_0->destinationBounds_0.z - kernelContext_0->tile_0->destinationBounds_0.x), uint(kernelContext_0->tile_0->destinationBounds_0.w - kernelContext_0->tile_0->destinationBounds_0.y));
}


#line 54
bool gripInsideTile_0(uint2 _S1, KernelContext_0 thread* kernelContext_1)
{

#line 54
    uint2 _S2 = gripTileSize_0(kernelContext_1);

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
float2 gripImagePixel_0(uint2 _S4, KernelContext_0 thread* kernelContext_2)
{

#line 62
    float4 d_0 = kernelContext_2->tile_0->destinationBounds_0;

#line 70
    if(((kernelContext_2->tile_0->reserved_0) & 1U) != 0U)
    {
        return float2(d_0.x + float(_S4.x) + 0.5f, d_0.y + float(_S4.y) + 0.5f);
    }
    return float2(d_0.x + float(_S4.x) + 0.5f, d_0.w - float(_S4.y) - 0.5f);
}


#line 484 "core"
float2 gripTexCoord_0(uint _S5, float2 _S6, KernelContext_0 thread* kernelContext_3)
{

#line 81 "../../kernels/GripKernel.slang"
    if(_S5 >= 16U)
    {
        return float2(0.0f) ;
    }

    return float2(dot(kernelContext_3->tile_0->textures_0[_S5].u_0.xy, _S6) + kernelContext_3->tile_0->textures_0[_S5].u_0.z, dot(kernelContext_3->tile_0->textures_0[_S5].v_0.xy, _S6) + kernelContext_3->tile_0->textures_0[_S5].v_0.z);
}


#line 47 "Spot.slang"
[[kernel]] void spotKernel(uint3 tid_0 [[thread_position_in_grid]], GripTile_0 constant* tile_1 [[buffer(1)]], texture2d<float, access::sample> source_1 [[texture(0)]], sampler bilinear_1 [[sampler(0)]], SpotUniforms_0 constant* uniforms_1 [[buffer(0)]], texture2d<float, access::read_write> destination_1 [[texture(1)]])
{

#line 47
    thread KernelContext_0 kernelContext_4;

#line 47
    (&kernelContext_4)->tile_0 = tile_1;

#line 47
    (&kernelContext_4)->source_0 = source_1;

#line 47
    (&kernelContext_4)->bilinear_0 = bilinear_1;

#line 47
    (&kernelContext_4)->uniforms_0 = uniforms_1;

#line 47
    (&kernelContext_4)->destination_0 = destination_1;

    uint2 _S7 = tid_0.xy;

#line 49
    bool _S8 = gripInsideTile_0(_S7, &kernelContext_4);

#line 49
    if(!_S8)
    {
        return;
    }

#line 51
    float2 _S9 = gripImagePixel_0(_S7, &kernelContext_4);

#line 51
    float2 _S10 = gripTexCoord_0(0U, _S9, &kernelContext_4);


    float4 color_0 = (((&kernelContext_4)->source_0).sample(((&kernelContext_4)->bilinear_0), (_S10), level((0.0f))));

    float outside_0 = smoothstep((&kernelContext_4)->uniforms_0->radius_0, (&kernelContext_4)->uniforms_0->radius_0 + max((&kernelContext_4)->uniforms_0->softness_0, 0.00009999999747379f), length(_S9 - (&kernelContext_4)->uniforms_0->center_0));

#line 56
    float outside_1;
    if(INVERT_0)
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
    (&kernelContext_4)->destination_0.write(float4(color_0.xyz * float3(mix(1.0f, (&kernelContext_4)->uniforms_0->outsideGain_0, outside_1))  * (&kernelContext_4)->uniforms_0->tint_0, color_0.w),_S7);
    return;
}

