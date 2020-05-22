#version 450
#include "../generation.glsl"

const bool optimiseGeneration = true;

float ComputeCumulusMask(vec3 p)
{
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
    return smoothstep(0.0, 0.5, mask);
}


float SmoothstepAntiderivative(float t)
{
    return t*t*t - t*t*t*t/2.0;
}
// integrate smoothstep with edges on [0, 1]
float IntegrateSmoothstep(float a, float b)
{
    return SmoothstepAntiderivative(b) - SmoothstepAntiderivative(a);
}


float ComputeMieDensity(DensityComputationParams params)
{
    vec3 p = params.p;
    float h = params.h;
    float mie = 0.0;
    
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
        mie = -h / HM; // - too strong now? Check offset/scaling
        //mie = min(0.0, mie); // - testing
        mie = -20; // - testing
    }
    
    const vec3 animDir = cross(normalize(p), vec3(0.0, 0.0, 1.0)); // - experimental
    
    const bool 
        enableStratus = true,
        enableCumulus = true,
        enableCirrus = true;
        
    const float animationT = animationTime * 3.0
        //* 16.0 // - really fast (experimented with while taking pictures 1594-1595)
        ;
    
    if (enableCumulus)
    { // new noisy clouds attempt
        float d = 0.0;
        vec3 np = p;
        float a = 1.0;
        // - to do: find actual good frequency
        np *= 2.5;
        np *= 16.0;
        np *= 4.0;//2.0;
        
        
        float mask = 1.0;
        
        const float cloudsTop = 0.5; // 0.25 can be good for seeing the 3D-ness of the clouds (though they go too high)
        // - do these transitions need to depend on voxel size? Maybe. Think about it, and test
        // - they do, yes. Currently these two cause structural banding
        mask *= smoothstep(height * cloudsTop, height * 0.75, shellDist); 
        mask *= 1.0 - smoothstep(height * 0.85, height, shellDist); // - most of the banding from here? Seems like it might be
        mask *= shellFactor;
        if (mask > 0.0) mask *= ComputeCumulusMask(p);
        
        if (mask > 0.0)
        {
            // - testing "movement" over time:
            np += animDir * 1e-3 * animationT;
            
            d = (fBm(11u, np * 8.0, 0.5, 2.0) * 0.5 + 0.5); // - used to be 7 octaves (before getting used to higher detail)
            d -= 0.5; // - more broken up and dramatic (in high resolution)
            /*d = (fBmCellular(6u, np * 4.0, 0.5, 2.0) * 0.5 + 0.5); // - used to be 7 octaves (before getting used to higher detail)
            d -= 0.85; // - more broken up and dramatic (in high resolution)*/
            
            const float cloudDensity = 10.0; // - to do: tune this (probably needs to be higher, no? Maybe 10? Try to find a physical derivation)
            float cloud = cloudDensity * max(0.0, d);
            // - to do: try different ways of combining these
            mie = mix(mie, cloud, mask);
            //mie = log(mix(exp(mie), exp(cloud), shellFactor * mask));
            //mie += 2.0 * cloud * shellFactor * mask; // - is this (just adding) really better? Hmm.
        }
    }
    { // simple attempt at higher (smoother) cloud layer
        
        vec3 p2 = p - animDir * 5e-5 * animationT;
        vec3 p3 = p + animDir * 5e-5 * animationT;
        
        float mask = fBm(8u, p2 * 256.0, 0.5, 2.0); // simplistic
        //float mask = fBm(8u, p * 1024.0, 0.5, 2.0); // simplistic (dramatic)
        mask = mask * 0.5 + 0.5;
        mask = max(0.0, mask - 0.5);
        mask = 1.0;
        const float maskBase = mask;
        const float cloudsTop = 0.5; // 0.25 can be good for seeing the 3D-ness of the clouds (though they go too high)
        
        const float top = 0.75, bottom = 0.1;
        //mask *= smoothstep(height * cloudsTop, height * 0.75, shellDist); 
        //mask *= 1.0 - smoothstep(height * bottom, height, shellDist); // - most of the banding from here? Seems like it might be
        const float h = 1.0 - shellDist / height;
        const float 
            base = 0.5,//0.025,//0.25, 
            thickness = 0.025;
        //mask *= 1.0 - smoothstep(base, base + thickness * 3, h); // - top
        if (false) 
        {
            // This is a noticeably aliasing mask. But using the less aliasing one gives shadow artefacts now.
            mask *= 1.0 - clamp((h - base) / (thickness * 2.0), 0.0, 1.0);
            mask *= smoothstep(base - thickness, base, h); // - bottom
            //mask = max(0.0, mask - 0.125);
        }
        else   
        {
            // Attempt at less aliasing mask:
            const float voxelSize = params.voxelSize / height;
            float d = abs(h - base) / max(voxelSize * 2.0, thickness); // local distance to layer centre, in layer half extent
            d = abs(h - base) / thickness;
            mask *= 1.0 - smoothstep(0.0, 1.0, d);
            float v = voxelSize / thickness;
            mask = d < 1.0 + v * 0.5 ? 1.0 : 0.0; // - testing
            // - to do: try to make less opaque when on the border of the mask in lower resolution voxels
            
            { // - attempting integration over threshold function (double-sided smoothstep):
                mask = 0.0;
                vec2 f;
                d = (h - base) / thickness;
                vec2 dd = vec2(d) + v*0.5*vec2(-1.0, 1.0);
                
                f = vec2(1.0) + clamp(dd, vec2(-1.0), vec2(0.0)); // negative
                mask += IntegrateSmoothstep(f.x, f.y);
                
                f = vec2(1.0) - clamp(dd, vec2( 0.0), vec2(1.0)); // positive
                mask += IntegrateSmoothstep(f.y, f.x);
                
                mask /= v*1.0;
            }
        }
        
        
        if (!optimiseGeneration || mask > 0.0)
        if (enableCirrus)
        { // cirrus
            float d = fBm(4u, (vec3(0.65) + p2) * 1024.0, 0.5, 2.0) * 0.5 + 0.5;
            {
                vec3 p = (vec3(0.65) + p2) * 32.0;
                float a = 1.0;
                float v = 0.0;
                for (uint i = 0u; i < 8u; ++i)
                {
                    v += a * (noise(p) * 2.0 - 1.0);
                    a *= 0.5;
                    p *= 2.0 * animDir;
                }
                d = v;
            }
            d += fBm(6u, (vec3(0.65) + p2) * 256.0, 0.5, 2.0);
            //d = 1.0;
            //d *= 0.5; // - worked well for fog
            //d *= 0.0625; // stratus or cirrus
            //d *= 0.125; // - not doing this makes for dramatic shadows
            
            d *= pow(2.0, -1.5);
            mie = max(mie, d * mask);
        }
        if (enableStratus)
        { // stratus (low, i.e. fog or mist)
            //float mask = maskBase;
            float mask = fBm(4u, (p + vec3(0.6)) * 512.0, 0.5, 2.0); // simplistic
            //float mask = fBm(8u, p * 1024.0, 0.5, 2.0); // simplistic (dramatic)
            mask = mask * 0.5 + 0.5;
            mask = max(0.0, mask - 0.25);
            
            const float 
                base = 0.0,//0.025,//0.25, 
                thickness = 0.05;
            //mask *= 1.0 - smoothstep(base, base + thickness * 3, h); // - top
            mask *= 1.0 - clamp((h - base) / (thickness * 2.0), 0.0, 1.0);
            //mask *= smoothstep(base - thickness, base, h); // - bottom
            mask = max(0.0, mask);// - 0.25);
         
            if (!optimiseGeneration || mask > 0.0)
            {
                float d = fBm(4u, (vec3(0.3126) + p3) * 1024.0, 0.5, 2.0) * 0.5 + 0.5;
                //d = 1.0;
                d *= 0.5; // - worked well for fog
                //d *= 0.125; // stratus or cirrus
                
                mie = max(mie, d * mask);
            }
        }
    }
    //mie *= 0.5; // - testing. Does seem to produce nicely diffuse clouds, at least in combination with raised Mie light intensity
    //mie *= 0.0625; // - testing
    
    return mie;
}
