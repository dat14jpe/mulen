#version 450

#include "../noise.glsl"
#include "../geometry.glsl"
#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, r16f) writeonly image3D lightImage;

// p in [-1, 1] range over entire atmosphere octree
float SampleLighting(vec3 p)
{
    // - this could easily use a less general approach (maybe a list of immediate node group neighbours from the CPU)
    vec3 nodeCenter;
    float nodeSize;
    uint depth;
    uint ni = OctreeDescendMap(p, nodeCenter, nodeSize, depth);
    vec3 lc = (p - nodeCenter) / nodeSize * 0.5 + 0.5;
    const vec3 brickOffs = vec3(BrickIndexTo3D(ni));
    vec3 tc = BrickSampleCoordinates(brickOffs, lc);
    return texture(brickLightTexture, tc).x;
}

void main()
{
    const uint loadId = GetWorkGroupIndex();
    const UploadBrick upload = uploadBricks[loadId];
    uvec3 writeOffs = BrickIndexTo3D(upload.brickIndex) * BrickRes + gl_LocalInvocationID;
    
    vec3 lp = vec3(gl_LocalInvocationID) / float(BrickRes - 1u) * 2 - 1;
    vec3 gp = (upload.nodeLocation.xyz + upload.nodeLocation.w * lp);
    vec3 p = gp * atmosphereScale;
    
    
    const float voxelSize = 1.0 / float(BrickRes - 1u) * 2 * upload.nodeLocation.w * atmosphereScale * planetRadius;
    
    vec3 ori = p * planetRadius;
    const float H = length(ori - planetLocation);
    if (H > planetRadius + atmosphereHeight + voxelSize) return; // outside atmosphere
    if (H < planetRadius - voxelSize) return; // inside planet
    
    // - maybe to do: try to optimise by using explicit knowledge of neighbour samples in the same node/brick
    float light = SampleLighting(gp);
    
    //if (false)
    {
        light = 0.0;
        int res = 2;
        for (int z = 0; z < res; ++z)
        for (int y = 0; y < res; ++y)
        for (int x = 0; x < res; ++x)
        {
            vec3 offs = vec3(ivec3(x, y, z)) / float(res - 1) * 2.0 - 1.0;
            const float offsFactor = 0.5; // - to do: make sure that this is sensible
            
            // (for now, it seems to brighten clouds up too much, probably from the unshadowed air above them)
            // Maybe there should be an attempt to sample in a cone directed towards the light, instead of cubically?
            
            offs = offs * dot(lightDir, offs); // - experimental
            vec3 p = gp + offs * voxelSize / atmosphereScale / planetRadius * offsFactor;
            light += SampleLighting(p);
        }
        light /= float(res * res * res);
    }
    
    imageStore(lightImage, ivec3(writeOffs), vec4(light, vec3(0.0)));
}
