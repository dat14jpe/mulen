#version 450

#include "../noise.glsl"
#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = 1, local_size_z = 1) in;
#include "compute.glsl"

uniform uint brickUploadOffset;

void main()
{
    const uint loadId = GetWorkGroupIndex() + brickUploadOffset;
    
    // - to do: compute layer constancy, store to shared memory
    // - one invocation gets to collate the entirety
}
