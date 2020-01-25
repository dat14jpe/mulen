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
    vec3 p = (upload.nodeLocation.xyz + upload.nodeLocation.w * lp) * atmosphereScale;
    
    float dist = 0.0;
    
    // - to do: generate or copy data
    
    
    { // 8 spheres
        dist = 0.0;
        for (float z = -1.0; z <= 1.0; z += 2.0)
        for (float y = -1.0; y <= 1.0; y += 2.0)
        for (float x = -1.0; x <= 1.0; x += 2.0)
        {
            float d = 1.0 - length(2 * (p - vec3(x, y, z) * 0.5));
            dist = max(dist, d);
        }
    }
    
    { // noisy
        dist = 0.0;
        vec3 np = p;
        float a = 1.0;
        // - to do: find actual good frequency
        np *= 2.5;
        np *= 4.0;
        np *= 4.0;
        np *= 2.0;
        for (uint i = 0u; i < 8u; ++i)
        {
            dist += a * noise(np);
            np *= 2.0;
            a *= 0.5;
        }
        dist -= 0.5;
        
        // Shape into spherical shell:
        dist = 1.0;
        const float height = 0.02; // - to do: aim for approximately 0.02 (Earth-like)
        const float shellDist = 1.0 + height - length(p);
        // - to do: change thresholds at low levels of detail
        dist *= smoothstep(0.0, height, shellDist); // outer
        dist *= 1.0 - smoothstep(height, height + 0.05, shellDist); // inner
    }
    
    { // zero faces
        for (uint d = 0u; d < 3u; ++d)
        {
            if (abs(p[d]) == 1.0) dist = 0.0;
        }
    }
    
    dist = clamp(dist, 0.0, 1.0);
    imageStore(brickImage, ivec3(writeOffs), vec4(dist, 0, 0, 0));
}
