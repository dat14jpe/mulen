#version 450

#include "../noise.glsl"
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
    {
        vec3 m = fract((p * 0.5 + 0.5) * 1.0);
        //dist = m.x < 0.5 && m.y < 0.5 && m.z < 0.5 ? 0.5 : 0.0;
        //dist = 1.0 - 2.0 * distance(m, vec3(0.5));
        
        uvec3 loc = uvec3(gl_LocalInvocationID) % uvec3(3.0);
        if (loc.x < 1u && loc.y < 1u && loc.z < 1u) dist = 0.5;
        //dist = 1.0 - length(p);
        
        //dist = 1.0 - length(lp);
        
        dist = 0.0;
        for (float z = -1.0; z <= 1.0; z += 2.0)
        for (float y = -1.0; y <= 1.0; y += 2.0)
        for (float x = -1.0; x <= 1.0; x += 2.0)
        {
            float d = 1.0 - length(2 * (p - vec3(x, y, z) * 0.5));
            dist = max(dist, d);
        }
    }
    
    dist = clamp(dist, 0.0, 1.0);
    imageStore(brickImage, ivec3(writeOffs), vec4(dist, 0, 0, 0));
}
