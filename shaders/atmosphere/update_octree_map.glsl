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
    //const uint maxDepth = 5u; // - to do: make this the 2-logarithm of the resolution
    // - to do: enable acceleration using the octree-spanning map when making smaller (i.e. per-frustum) maps
    OctreeTraversalData o;
    o.p = p;
    OctreeDescendMaxDepth(o, maxDepth);
    const uint ni = o.ni;
    uint gi = nodeGroups[ni / NodeArity].nodes[ni % NodeArity].children;
    gi &= IndexMask;
    if (InvalidIndex == gi)
    {
        gi = ni / NodeArity;
        o.depth -= 1u;
    }
    
    uint result = gi;
    for (uint i = 0u; i < NodeArity; ++i)
    {
        uint gi = nodeGroups[gi].nodes[i].children & IndexMask;
        if (gi != InvalidIndex)
        {
            result |= 1u << (i + IndexBits);
        }
    }
    result |= o.depth << (IndexBits + ChildBits);
    imageStore(octreeMapImage, ivec3(writeOffs), uvec4(result));
}
