#version 450

#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

void main()
{
    const uint loadId = GetWorkGroupIndex();
    const UploadNode upload = uploadNodes[loadId];
    
    // - to do: copy from upload brick data to actual brick texture
}
