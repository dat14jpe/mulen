#version 450

#include "../noise.glsl"
#include "common.glsl"
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
#include "compute.glsl"

uniform layout(binding=0, r32ui) writeonly uimage3D octreeMapImage;
uniform vec3 resolution;
uniform uint maxDepth;

void main()
{
    const uvec3 writeOffs = gl_GlobalInvocationID;
    //const vec3 unitPos = (vec3(writeOffs) + vec3(0.5)) / resolution;
    const vec3 p = (vec3(writeOffs) + vec3(0.5)) * mapScale + mapPosition;
    vec3 nodeCenter;
    float nodeSize;
    uint depth;
    //const uint maxDepth = 5u; // - to do: make this the 2-logarithm of the resolution
    // - to do: enable acceleration using the octree-spanning map when making smaller (i.e. per-frustum) maps
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
    imageStore(octreeMapImage, ivec3(writeOffs), uvec4(result));
}
