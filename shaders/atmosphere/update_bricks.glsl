#version 450

#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, rg8) writeonly image3D brickImage;

void main()
{
    const uint loadId = GetWorkGroupIndex();
    const UploadBrick upload = uploadBricks[loadId];
    uvec3 writeOffs = BrickIndexTo3D(upload.brickIndex) * BrickRes + gl_LocalInvocationID;
    
    vec3 lp = vec3(gl_LocalInvocationID) / float(BrickRes - 1u) * 2 - 1;
    vec3 p = upload.nodeLocation.xyz + upload.nodeLocation.w * lp;
    
    float dist = 0.0;
    // - to do: generate or copy data
    //vec3 m = fract(p * 2.0);
    //dist = m.x < 0.5 && m.y < 0.5 && m.z < 0.5 ? 0.5 : 0.0;
    uvec3 loc = uvec3(gl_LocalInvocationID) % uvec3(3.0);
    if (loc.x < 1u && loc.y < 1u && loc.z < 1u) dist = 0.5;
    
    imageStore(brickImage, ivec3(writeOffs), vec4(dist, 0, 0, 0));
}
