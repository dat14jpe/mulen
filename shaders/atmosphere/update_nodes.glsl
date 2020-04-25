#version 450

#include "common.glsl"
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
#include "compute.glsl"


void main()
{
    const uint loadId = GetGlobalIndex();
    UploadNodeGroup upload = uploadNodes[loadId];
    for (uint ci = 0u; ci < NodeArity; ++ci)
    {
        upload.nodeGroup.nodes[ci].children &= IndexMask;
    }
    nodeGroups[upload.groupIndex] = upload.nodeGroup;
}
