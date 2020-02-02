#version 450

#include "../noise.glsl"
#include "../geometry.glsl"
#include "common.glsl"
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
#include "compute.glsl"

uniform layout(binding=0, r32ui) writeonly uimage3D octreeMapImage;
uniform vec3 resolution;

void main()
{
    const uvec3 writeOffs = gl_GlobalInvocationID;
    const vec3 p = (vec3(writeOffs) + vec3(0.5)) / resolution * 2.0 - 1.0;
    vec3 nodeCenter;
    float nodeSize;
    uint depth;
    const uint maxDepth = 5u; // - to do: depend on resolution
    uint ni = OctreeDescendMaxDepth(p, nodeCenter, nodeSize, depth, maxDepth);
    uint gi = nodeGroups[ni / NodeArity].nodes[ni % NodeArity].children;
    if (InvalidIndex == gi)
    {
        gi = ni / NodeArity;
        depth -= 1u;
    }
    
    uint result = gi;
    for (uint i = 0u; i < NodeArity; ++i)
    {
        if (nodeGroups[gi].nodes[i].children != InvalidIndex)
        {
            result |= 1u << (i + IndexBits);
        }
    }
    result |= depth << (IndexBits + ChildBits);
    
    //result = 1u; // - debugging
    
    imageStore(octreeMapImage, ivec3(writeOffs), uvec4(result));
}
