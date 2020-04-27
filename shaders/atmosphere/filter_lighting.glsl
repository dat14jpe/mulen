#version 450

#include "../noise.glsl"
#include "common.glsl"
const uint Padding = 1;
const uint P2 = Padding * 2;
const uint Res = LightBrickRes;
layout(local_size_x = Res + P2, local_size_y = Res + P2, local_size_z = Res + P2) in;
#include "compute.glsl"

shared float lightSamples[(Res + P2) * (Res + P2) * (Res + P2)];

uniform layout(binding=0, r16f) writeonly image3D lightImage;

// p in [-1, 1] range over entire atmosphere octree
float SampleLighting(vec3 p)
{
    // - this could easily use a less general approach (maybe a list of immediate node group neighbours from the CPU)
    OctreeTraversalData o;
    o.p = p;
    OctreeDescendMap(o); // - to do: limit depth
    vec3 lc = (p - o.center) / o.size * 0.5 + 0.5;
    const vec3 brickOffs = vec3(BrickIndexTo3D(o.ni / NodeArity));
    /*lc = (lc + vec3(uvec3(o.ni, o.ni >> 1u, o.ni >> 2u) & uvec3(1u))) * 0.5;
    vec3 tc = BrickSampleCoordinates(brickOffs, lc, LightBrickRes);
    return texture(brickLightTexture, tc).x;*/
    
    return SampleBrick(brickLightTexture, o.ni, lc, brickOffs, LightBrickRes).r;
}

void main()
{
    const uint loadId = GetWorkGroupIndex() + brickUploadOffset;
    const UploadBrick upload = uploadBricks[loadId];
    uvec3 writeOffs = BrickIndexTo3D(upload.groupIndex) * Res + (gl_LocalInvocationID - uvec3(Padding));
    const vec3 brickOffs = vec3(BrickIndexTo3D(upload.groupIndex));
    
    const float localVoxelSize = 2.0 / float(Res - 1u);
    vec3 lp = vec3(ivec3(gl_LocalInvocationID) - ivec3(Padding)) / float(Res - 1u) * 2 - 1;
    const vec3 nodeCenter = upload.nodeLocation.xyz;
    const float nodeSize = upload.nodeLocation.w;
    vec3 gp = nodeCenter + nodeSize * lp;
    vec3 p = gp * atmosphereScale;
    
    const uint localIndex = gl_LocalInvocationID.x + (Res + P2) * (gl_LocalInvocationID.y + gl_LocalInvocationID.z * (Res + P2));
    
    const float voxelSize = 2.0 / float(Res - 1u) * upload.nodeLocation.w * atmosphereScale * planetRadius;
    
    vec3 ori = p * planetRadius;
    const float H = length(ori - planetLocation);
    const bool notInShell = 
        H > planetRadius + atmosphereHeight + voxelSize || // outside atmosphere
        H < planetRadius - voxelSize; // inside planet;
    lightSamples[localIndex] = notInShell ? 1.0 : SampleLighting(gp);
    float light = 1.0;
    
    // - maybe to do: try to optimise by using explicit knowledge of neighbour samples in the same node/brick
    memoryBarrierShared();
    barrier();
    
    if (notInShell) return;
    if (any(lessThan(gl_LocalInvocationID, uvec3(Padding))) || any(greaterThanEqual(gl_LocalInvocationID, uvec3(Res + Padding))))
    {
        return; // this is a border voxel
    }
    
    //if (false) // shared memory optimisation
    {
        light = 0.0;
        int res = 3;
        for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            // - to do: try different weights? Yes. To do
            float w = 1.0 / float(res * res * res);
            const float sigma = 0.75;
            float c = 1.0 / sqrt(2 * 3.141592653589793 * sigma*sigma);
            w = c*c*c * exp(-float(x*x + y*y + z*z) / (2.0*sigma*sigma));
            
            light += w * lightSamples[localIndex + x + (Res + P2) * (y + (Res + P2) * z)];
        }
    }
    // - to do: try sampling this voxel's density value and make the shadow less dark if it's non-zero
    // (i.e., very crudely "approximate" higher order scattering in the clouds)
    
    #ifdef toBeUpdatedMaybe
    if (false)
    {
        light = 0.0;
        int res = 2;
        for (int z = 0; z < res; ++z)
        for (int y = 0; y < res; ++y)
        for (int x = 0; x < res; ++x)
        {
            vec3 offs = vec3(ivec3(x, y, z)) / float(res - 1) * 2.0 - 1.0;
            const float offsFactor = 0.5; // - to do: make sure that this is sensible
            offs *= offsFactor;
            
            // (for now, it seems to brighten clouds up too much, probably from the unshadowed air above them)
            // Maybe there should be an attempt to sample in a cone directed towards the light, instead of cubically?
            
            //offs = offs * max(0.0, abs(dot(lightDir, normalize(offs)))); // - experimental
            
            vec3 p = lp + offs * localVoxelSize;
            if (!any(greaterThan(abs(p), vec3(1.0))))
            {
                vec3 lc = p * 0.5 + 0.5;
                vec3 tc = BrickSampleCoordinates(brickOffs, lc);
                light += texture(brickLightTexture, tc).x;
            }
            else // we must sample in another brick
            {
                light += SampleLighting(p * nodeSize + nodeCenter);
            }
        }
        light /= float(res * res * res);
    }
    #endif
    
    imageStore(lightImage, ivec3(writeOffs), vec4(light, vec3(0.0)));
}
