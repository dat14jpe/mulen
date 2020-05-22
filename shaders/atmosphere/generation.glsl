
#include "../noise.glsl"
#include "../3d-cnoise.frag"
#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, rg8) writeonly image3D brickImage;
uniform uint brickUploadOffset;

float fBm(uint octaves, vec3 p, float persistence, float lacunarity)
{
    float a = 1.0;
    float v = 0.0;
    for (uint i = 0u; i < octaves; ++i)
    {
        v += a * (noise(p) * 2.0 - 1.0);
        a *= persistence;
        p *= lacunarity;
    }
    return v;
}
float fBmCellular(uint octaves, vec3 p, float persistence, float lacunarity)
{
    float a = 1.0;
    float v = 0.0;
    for (uint i = 0u; i < octaves; ++i)
    {
        v += a * (cellular(p).y * 2.0 - 1.0);
        a *= persistence;
        p *= lacunarity;
    }
    return v;
}

struct DensityComputationParams
{
    float voxelSize;
    vec3 p;
    float h;
};
float ComputeMieDensity(DensityComputationParams);

void main()
{
    const uint loadId = GetWorkGroupIndex() + brickUploadOffset;
    const UploadBrick upload = uploadBricks[loadId];
    const uvec3 voxelOffs = BrickIndexTo3D(upload.brickIndex) * BrickRes + gl_LocalInvocationID;
    
    vec3 lp = vec3(gl_LocalInvocationID) / float(BrickRes - 1u) * 2 - 1;
    vec3 p = (upload.nodeLocation.xyz + upload.nodeLocation.w * lp) * atmosphereScale;
    const float voxelSize = /*planetRadius **/ atmosphereScale * upload.nodeLocation.w / float(BrickRes - 1u) * 2.0;
    
    if (false)
    {
        // generation jitter vs banding:
        // (didn't seem to really work well, unfortunately)
        p += upload.nodeLocation.w / float(BrickRes - 1u) * vec3(rand3D(p.xyz), rand3D(p.zyx), rand3D(p.yxz));
    }
    // - debugging generation aliasing:
    {
        //if (length(p) < 1.0) p = normalize(p);
        // - to do: try "normalizing" to nearest spherical shell of radius spacing voxelSize
    }
    
    const float h = (length(p) - 1.0) * planetRadius;
    
    if (h < -2e4) return; // - to do: check this
    if (h > Rt - Rg + 4e4) return;
    
    // - to do: some more computation (altitude, et cetera) before passing on the computation
    
    DensityComputationParams params;
    params.voxelSize = voxelSize;
    params.p = p;
    params.h = h;
    float mie = ComputeMieDensity(params);
    
    mie = (mie - offsetM) / scaleM;
    if (mie > 0.0) mie += abs(rand3D(p)) / 255.0; // - dither test
    imageStore(brickImage, ivec3(voxelOffs), vec4(clamp(mie, 0.0, 1.0), vec3(0.0)));
}
