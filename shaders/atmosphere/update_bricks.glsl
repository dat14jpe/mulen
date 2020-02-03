#version 450

#include "../noise.glsl"
#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, rg8) writeonly image3D brickImage;

float fBm(uint octaves, vec3 p, float persistence, float lacunarity)
{
    float a = 1.0;
    float v = 0.0;
    for (uint i = 0u; i < octaves; ++i)
    {
        v += a * (noise(p) * 2.0 - 1.0);
        a *= persistence;
        p *= lacunarity;
    }
    return v;
}

void main()
{
    const uint loadId = GetWorkGroupIndex();
    const UploadBrick upload = uploadBricks[loadId];
    uvec3 writeOffs = BrickIndexTo3D(upload.brickIndex) * BrickRes + gl_LocalInvocationID;
    
    vec3 lp = vec3(gl_LocalInvocationID) / float(BrickRes - 1u) * 2 - 1;
    vec3 p = (upload.nodeLocation.xyz + upload.nodeLocation.w * lp) * atmosphereScale;
    
    if (false)
    {
        // generation jitter vs banding:
        // (didn't seem to really work well, unfortunately)
        p += upload.nodeLocation.w / float(BrickRes - 1u) * vec3(rand3D(p.xyz), rand3D(p.zyx), rand3D(p.yxz));
    }
    
    float rayleigh = 0.0, mie = 0.0;
    
    // Generate clouds:
    // - to do: allow for simulation/update, and parameters from CPU
    
    const float h = (length(p) - 1.0) * planetRadius;
    const float height = 0.01; // - to do: aim for approximately 0.01 (Earth-like)
    const float shellDist = 1.0 + height - length(p);
    if (shellDist < 0.0) return; // - early exit if outside atmosphere (to do: check accuracy of this)
    float shellFactor = 0.0;
    { // noisy
        float dist = 0.0;
        /*vec3 np = p;
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
        dist -= 0.5;*/
        
        // Shape into spherical shell:
        dist = 1.0;
        // - to do: change thresholds at low levels of detail
        dist *= smoothstep(0.0, height, shellDist); // outer
        dist *= 1.0 - smoothstep(height, height + 0.05, shellDist); // inner
        shellFactor = dist;
        
        //dist *= 1.0 / 32.0; // - testing: "normal" atmosphere in smaller and lower part of range
        rayleigh = dist;
        rayleigh = -h / HR;
        mie = -h / HM; // - too strong now? Check offset/scaling
        mie = min(0.0, mie); // - testing
        //mie = -20; // - testing
    }
    { // new noisy clouds attempt
        float d = 0.0;
        vec3 np = p;
        float a = 1.0;
        // - to do: find actual good frequency
        np *= 2.5;
        np *= 16.0;
        np *= 4.0;//2.0;
        d = fBm(4u, np, 0.5, 2.0);
        
        /*
        np *= 1.0 / 64.0;
        for (uint i = 0u; i < 4u; ++i)
        {
            d += a * (cos(noise(np)) * 0.5 + 0.5);
            a *= 0.5;
            np *= 2.0;
        }
        */
        //d = 1.0; // - testing
        d = (fBm(7u, np, 0.5, 2.0) * 0.5 + 0.5) * 0.5 + 0.5;
        
        float mask = 0.0;
        mask = fBm(9u, p * 16.0, 0.5, 2.0); // simplistic
        {
            const float numCells = 3.0; // number on southern/northern hemisphere
            
            // - experiment: assign wind direction and compute mask with consideration to it
            
            float lat = asin(p.y / length(p));
            const float PI = 3.141592653589793;
            float y = lat / PI * 2.0;
            int cell = int(abs(trunc(y * numCells)));
            int cmi = cell % 2;
            float cellDist = abs(float(cell + cmi) - abs(y * numCells));
            float cm = float(cmi) * 2.0 - 1.0;
            vec2 localWind = vec2(-cm, sign(lat) * cm); // - change x magnitude depending on vertical location in cell? To 
            localWind.x = mix(localWind.x, localWind.x * cellDist, 0.75); // - to do: tune
            // - to do: smooth transition between cells
            
            const vec3 up = vec3(0, 1, 0);
            const vec3 N = normalize(p);
            const vec3 T = cross(up, N);
            vec3 wind = localWind.x * T + localWind.y * cross(N, T);
            
            float scaleBias = 0.05; // - to do: adjust
            vec3 scale = sign(wind) / (abs(2.0 * wind) + vec3(scaleBias));
            //vec3 scale = 4.0 * wind;//vec3(1.0) / (wind + vec3(scaleBias));
            const uint octaves = 5u;
            const float persistence = 0.5;
            const float lacunarity = 2.0;
            const vec3 op = p;
            {
                vec3 p = op * 16.0;
                float a = 1.0;
                mask = 0.0;
                for (uint i = 0u; i < octaves; ++i)
                {
                    mask += a * (noise(p) * 2.0 - 1.0);
                    a *= persistence;
                    p *= lacunarity * scale;
                }
            }
            // Unfinished attempt at intertropical convergence zone:
            //mask += (1.0 - smoothstep(0.0, 0.03, abs(y))) * (fBm(3u, p * 8.0, 0.5, 2.0) * 0.5 + 0.5);
            
            //mask = cm; // - debugging
            
            // - to do: construct wind-aware mask
        }
        mask = smoothstep(0.0, 0.5, mask);
        
        const float cloudsTop = 0.5; // 0.25 can be good for seeing the 3D-ness of the clouds (though they go too high)
        // - do these transitions need to depend on voxel size? Maybe. Think about it, and test
        // - they do, yes. Currently these two cause structural banding
        //mask *= smoothstep(height * cloudsTop, height * 0.75, shellDist); 
        //mask *= 1.0 - smoothstep(height * 0.95, height, shellDist); // - most of the banding from here? Seems like it might be
        
        mie = mix(mie, 10.0 * max(0.0, d), shellFactor * mask);
    }
    
    { // zero faces
        for (uint d = 0u; d < 3u; ++d)
        {
            if (abs(p[d]) == 1.0) rayleigh = mie = 0.0;
        }
    }
    
    rayleigh = (rayleigh - offsetR) / scaleR;
    mie = (mie - offsetM) / scaleM;
    imageStore(brickImage, ivec3(writeOffs), vec4(clamp(vec2(rayleigh, mie), vec2(0.0), vec2(1.0)), 0, 0));
}
