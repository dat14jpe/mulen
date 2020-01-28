#version 450

#include "../noise.glsl"
#include "../geometry.glsl"
#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, rgba16f) writeonly image3D brickImage;

void main()
{
    const uint loadId = GetWorkGroupIndex();
    const UploadBrick upload = uploadBricks[loadId];
    uvec3 writeOffs = BrickIndexTo3D(upload.brickIndex) * BrickRes + gl_LocalInvocationID;
    
    vec3 lp = vec3(gl_LocalInvocationID) / float(BrickRes - 1u) * 2 - 1;
    vec3 gp = (upload.nodeLocation.xyz + upload.nodeLocation.w * lp);
    vec3 p = gp * atmosphereScale;
    
    vec3 light = vec3(0.0);
    {
        // - to do: ray trace towards light source
        // - to do: maybe decrease planet radius by approximately one voxel length, to avoid overshadowing?
        const float voxelSize = 1.0 / float(BrickRes - 1u) * 2 * upload.nodeLocation.w * atmosphereScale * planetRadius;
        float R = planetRadius;// - voxelSize; // - to do: try making shadowing gradual instead
        vec3 ori = p * planetRadius;
        vec3 dir = lightDir;
        float t0, t1;
        if (!IntersectSphere(ori, dir, vec3(0.0), R, t0, t1))
        {
            light = vec3(1.0);
        }
    }
    
    imageStore(brickImage, ivec3(writeOffs), vec4(light, 0.0));
}
