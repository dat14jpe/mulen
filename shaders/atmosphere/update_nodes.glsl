#version 450

#include "common.glsl"
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
#include "compute.glsl"


void main()
{
    const uint loadId = GetGlobalIndex();
    UploadNodeGroup upload = uploadNodes[loadId];
    
    nodeGroups[upload.groupIndex] = upload.nodeGroup;
    
    // - to do: handle linkage correctly
    // - if type == Split, update node group but not any one node (nor any bricks)
}
