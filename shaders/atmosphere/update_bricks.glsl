#version 450

#include "../noise.glsl"
#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, rg8) writeonly image3D brickImage;
uniform uint brickUploadOffset;

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
    const uint loadId = GetWorkGroupIndex() + brickUploadOffset;
    const UploadBrick upload = uploadBricks[loadId];
    const uvec3 voxelOffs = BrickIndexTo3D(upload.brickIndex) * BrickRes + gl_LocalInvocationID;
    
    vec3 lp = vec3(gl_LocalInvocationID) / float(BrickRes - 1u) * 2 - 1;
    vec3 p = (upload.nodeLocation.xyz + upload.nodeLocation.w * lp) * atmosphereScale;
    const vec3 op = p;
    
    if (false)
    {
        // generation jitter vs banding:
        // (didn't seem to really work well, unfortunately)
        p += upload.nodeLocation.w / float(BrickRes - 1u) * vec3(rand3D(p.xyz), rand3D(p.zyx), rand3D(p.yxz));
    }
    // - debugging generation aliasing:
    {
        //if (length(p) < 1.0) p = normalize(p);
        // - to do: try "normalizing" to nearest spherical shell of radius spacing voxelSize
    }
    
    float rayleigh = 0.0, mie = 0.0;
    
    // Generate clouds:
    // - to do: allow for simulation/update, and parameters from CPU
    
    const float h = (length(p) - 1.0) * planetRadius;
    
    if (h < -2e4) return; // - to do: check this
    if (h > Rt - Rg + 4e4) return;
    
    const float height = 0.5 * 0.01; // - to do: aim for approximately 0.01 (Earth-like)
    const float shellDist = 1.0 + height - length(p);
    //if (shellDist < 0.0) return; // - early exit if outside atmosphere (to do: check accuracy of this)
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
        //mie = min(0.0, mie); // - testing
        mie = -20; // - testing
    }
    
    const vec3 animDir = cross(normalize(p), vec3(0.0, 0.0, 1.0)); // - experimental
    
    //if (false) // - debugging
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
        
        // - testing "movement" over time:
        np += animDir * 2e-3 * animationTime;
        
        d = (fBm(11u, np * 16.0, 0.5, 2.0) * 0.5 + 0.5); // - used to be 7 octaves (before getting used to higher detail)
        d -= 0.5; // - more broken up and dramatic (in high resolution)
        
        float mask = 0.0;
        mask = fBm(9u, p * 64.0, 0.5, 2.0); // simplistic
        if (false) // - testing
        {
            const float numCells = 3.0; // number on southern/northern hemisphere
            
            // - experiment: assign wind direction and compute mask with consideration to it
            
            float noisedY = p.y + (fBm(4, p * 8.0, 0.5, 2.0) * 2.0 - 1.0) * 0.25;
            
            float lat = asin(noisedY / length(p));
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
            //if (false)
            {
                vec3 p = op * 12.0;
                float a = 1.0;
                mask = 0.0;
                for (uint i = 0u; i < octaves; ++i)
                {
                    mask += a * (noise(p) * 2.0 - 1.0);
                    a *= persistence;
                    p *= lacunarity * scale;
                }
            }
            //mask -= 0.25;
            // Unfinished attempt at intertropical convergence zone:
            //mask += (1.0 - smoothstep(0.0, 0.03, abs(y))) * (fBm(3u, p * 8.0, 0.5, 2.0) * 0.5 + 0.5);
            
            //mask = cm; // - debugging
            
            // - to do: construct wind-aware mask
        }
        mask = smoothstep(0.0, 0.5, mask);
        
        const float cloudsTop = 0.5; // 0.25 can be good for seeing the 3D-ness of the clouds (though they go too high)
        // - do these transitions need to depend on voxel size? Maybe. Think about it, and test
        // - they do, yes. Currently these two cause structural banding
        mask *= smoothstep(height * cloudsTop, height * 0.75, shellDist); 
        mask *= 1.0 - smoothstep(height * 0.85, height, shellDist); // - most of the banding from here? Seems like it might be
        
        const float cloudDensity = 10.0; // - to do: tune this (probably needs to be higher, no? Maybe 10? Try to find a physical derivation)
        float cloud = cloudDensity * max(0.0, d);
        // - to do: try different ways of combining these
        mie = mix(mie, cloud, shellFactor * mask);
        //mie = log(mix(exp(mie), exp(cloud), shellFactor * mask));
        //mie += 2.0 * cloud * shellFactor * mask; // - is this (just adding) really better? Hmm.
    }
    { // simple attempt at higher (smoother) cloud layer
        float mask = fBm(2u, p * 128.0, 0.5, 2.0); // simplistic
        mask = mask * 0.5 + 0.5;
        const float maskBase = mask;
        const float cloudsTop = 0.5; // 0.25 can be good for seeing the 3D-ness of the clouds (though they go too high)
        
        const float top = 0.75, bottom = 0.1;
        //mask *= smoothstep(height * cloudsTop, height * 0.75, shellDist); 
        //mask *= 1.0 - smoothstep(height * bottom, height, shellDist); // - most of the banding from here? Seems like it might be
        const float h = 1.0 - shellDist / height;
        const float 
            base = 0.5,//0.025,//0.25, 
            thickness = 0.05;
        //mask *= 1.0 - smoothstep(base, base + thickness * 3, h); // - top
        mask *= 1.0 - clamp((h - base) / (thickness * 2.0), 0.0, 1.0);
        mask *= smoothstep(base - thickness, base, h); // - bottom
        mask = max(0.0, mask - 0.25);
        
        vec3 p2 = p - animDir * 5e-5 * animationTime;
        vec3 p3 = p + animDir * 5e-5 * animationTime;
        
        { // cirrus
            float d = fBm(4u, (vec3(0.65) + p2) * 1024.0, 0.5, 2.0) * 0.5 + 0.5;
            //d = 1.0;
            //d *= 0.5; // - worked well for fog
            //d *= 0.0625; // stratus or cirrus
            d *= 0.125;
            
            mie = max(mie, d * mask);
        }
        { // stratus (low, i.e. fog or mist)
            float mask = maskBase;
            const float 
                base = 0.0,//0.025,//0.25, 
                thickness = 0.05;
            //mask *= 1.0 - smoothstep(base, base + thickness * 3, h); // - top
            mask *= 1.0 - clamp((h - base) / (thickness * 2.0), 0.0, 1.0);
            mask *= smoothstep(base - thickness, base, h); // - bottom
            mask = max(0.0, mask - 0.25);
            
            float d = fBm(4u, (vec3(0.3126) + p3) * 1024.0, 0.5, 2.0) * 0.5 + 0.5;
            //d = 1.0;
            d *= 0.5; // - worked well for fog
            //d *= 0.125; // stratus or cirrus
            
            mie = max(mie, d * mask);
        }
    }
    //mie *= 0.5; // - testing. Does seem to produce nicely diffuse clouds, at least in combination with raised Mie light intensity
    
    { // zero faces
        for (uint d = 0u; d < 3u; ++d)
        {
            if (abs(p[d]) == 1.0) rayleigh = mie = 0.0;
        }
    }
    //mie *= 0.0625; // - testing
    
    rayleigh = (rayleigh - offsetR) / scaleR;
    mie = (mie - offsetM) / scaleM;
    if (mie > 0.0) mie += abs(rand3D(op)) / 255.0; // - dither test
    vec2 data = vec2(rayleigh, mie); // - old order
    data = vec2(mie, rayleigh);
    imageStore(brickImage, ivec3(voxelOffs), vec4(clamp(data, vec2(0.0), vec2(1.0)), 0, 0));
}
