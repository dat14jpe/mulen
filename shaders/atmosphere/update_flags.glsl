#version 450

#include "../noise.glsl"
#include "common.glsl"
layout(local_size_x = 1, local_size_y = 1, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, r8) readonly image3D brickImage;
uniform uint brickUploadOffset;

shared vec2 layers[BrickRes]; // min and max density

void main()
{
    const uint loadId = GetWorkGroupIndex() + brickUploadOffset;
    const UploadBrick upload = uploadBricks[loadId];
    const uvec3 voxelOffs = BrickIndexTo3D(upload.brickIndex) * BrickRes + gl_LocalInvocationID;
    const uint layer = gl_LocalInvocationID.z;
    
    float minDensity = 1e30;
    float maxDensity = -1e30;
    for (uint y = 0u; y < BrickRes; ++y)
    for (uint x = 0u; x < BrickRes; ++x)
    {
        float density = imageLoad(brickImage, ivec3(voxelOffs + uvec3(x, y, 0u))).x;
        //density = texelFetch(brickTexture, ivec3(voxelOffs + uvec3(x, y, 0u)), 0).x; // - still same problem
        minDensity = min(minDensity, density);
        maxDensity = max(maxDensity, density);
    }
    layers[layer] = vec2(minDensity, maxDensity);
    memoryBarrierShared();
    barrier();
    
    if (layer != 0u) return; // only one invocation gets to handle the total
    
    const uint gi = upload.nodeIndex / NodeArity;
    const uint ni = upload.nodeIndex % NodeArity;
    
    for (uint z = 0u; z < BrickRes; ++z)
    {
        minDensity = min(minDensity, layers[z].x);
        maxDensity = max(maxDensity, layers[z].y);
    }
    const bool isEmpty = minDensity == 0.0 && maxDensity == 0.0;
    if (isEmpty) nodeGroups[gi].nodes[ni].children |= EmptyBrickBit;
    
    // - maybe to do: other flags
}
