#version 450

#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, rg8) image3D oldBrickImage;
//uniform uint brickUploadOffset;
uniform layout(binding=0) sampler3D oldBrickTexture;

void main()
{
    const uint wgid = GetWorkGroupIndex();
    const uint groupIndex = splitNodes[wgid / NodeArity];
    const uint octant = wgid % NodeArity;
    const uint brickIndex = groupIndex * NodeArity + octant;
    uvec3 writeOffs = BrickIndexTo3D(brickIndex) * BrickRes + gl_LocalInvocationID;
    const vec3 brickOffs = vec3(BrickIndexTo3D(brickIndex));
    
    vec3 lp = vec3(ivec3(gl_LocalInvocationID)) / float(BrickRes - 1u) * 2 - 1;
    vec3 pp = 0.5 * (lp + vec3(uvec3(octant & 1u, (octant >> 1u) & 1u, (octant >> 2u) & 1u)) * 2 - 1);
    const uint parentIndex = nodeGroups[groupIndex].parent;
    const vec3 parentBrickOffs = vec3(BrickIndexTo3D(parentIndex));
    
    // - to do: correct this. Not yet right
    vec4 parentValue = RetrieveVoxelData(parentBrickOffs, pp, oldBrickTexture);
    
    imageStore(oldBrickImage, ivec3(writeOffs), parentValue);
}
