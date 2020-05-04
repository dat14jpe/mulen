#version 450
#include "common.glsl"
#include "../noise.glsl"

/*layout(location = 0) out vec4 outValue;
in vec4 clip_coords;*/
uniform layout(binding=4) sampler2D lightTexture;

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
#include "compute.glsl"

uniform layout(binding=0, rgba16f) image2D lightImage;
uniform layout(binding=1, rgba16f) image2D transmittanceImage;


float GetDepth(vec4 clipCoords)
{
    float d = texture(depthTexture, clipCoords.xy * 0.5 + 0.5).x;
    d = exp2(d / Fcoef_half) - 1.0;
    return d;
}

vec3 ViewspaceFromDepth(vec4 clipCoords, float depth)
{
    vec4 cs = vec4(clipCoords.xy * depth, -depth, depth);
    vec4 vs = invProjMat * cs;
    return vs.xyz;
}

void ComputeBaseDensities(out float rayleighDensity, out float mieDensity, float r)
{
    float H = r - Rg;
    rayleighDensity = RayleighDensityFromH(H);
    mieDensity = MieDensityFromH(H);
}

vec3 TransmittanceFromPoint(vec3 p)
{
    float r = length(p);
    float mu = dot(p / r, normalize(lightDir.xyz)); // - about 0.3 ms faster *with* the unnecessary lightDir.xyz normalisation. Hmm...
    return vec3(GetSunOcclusion(r, mu)); // - just this for now
    //return GetTransmittanceToAtmosphereTop(r, mu); // - doesn't help;
    return GetTransmittanceToSun(r, mu);
}

uniform uvec2 fragOffset, fragFactor;

void main()
{
    const ivec2 ifragCoords = ivec2(fragOffset + fragFactor * gl_GlobalInvocationID.xy);
    const vec2 fragCoords = vec2(ifragCoords) + vec2(0.5);
    const vec2 coords = fragCoords / vec2(imageSize(lightImage));
    if (coords.x >= 1.0 || coords.y >= 1.0) return; // outside the target
    const vec4 clipCoords = vec4(coords * 2.0 - 1.0, 1.0, 1.0);
    
    vec3 backLight = texelFetch(lightTexture, ifragCoords, 0).xyz;
    //vec3 backLight = imageLoad(lightImage, ivec2(fragCoords), 0).xyz;
    const float atmScale = atmosphereRadius;
    
    const vec3 ori = vec3(invViewMat * vec4(0, 0, 0, 1));
    const vec3 dir = normalize(vec3(invViewProjMat * clipCoords));
    float solidDepth = length(ViewspaceFromDepth(clipCoords, GetDepth(clipCoords)));
    const float actualSolidDepth = solidDepth;
    
    float opticalDepthR = 0.0, opticalDepthM = 0.0;
    vec3 transmittance = vec3(1.0);
    vec3 color = vec3(0.0);
    
    AtmosphereIntersection ai = IntersectAtmosphere(ori, dir);
    
    const bool AddSecondOuter = true;//false;
    
    // - to do: fully correct and encompassing logic
    // - to do: consider depth buffer occlusion before inner atmosphere
    const float outerLength = ai.intersectsInner ? ai.innerMin - ai.outerMin : ai.outerMax - ai.outerMin;
    if (ai.intersectsOuter && outerLength > 0.0)
    {
        vec3 p = ori + dir * ai.outerMin - planetLocation.xyz;
        float r = length(p);
        float mu = dot(dir, p) / r;
        float mu_s = dot(lightDir.xyz, p) / r;
        float nu = dot(dir, lightDir.xyz);
        bool intersectsGround = false; // - to do
        //intersectsGround = solidDepth < outerMax;
        vec3 scattering = GetScattering(r, mu, mu_s, nu, intersectsGround);
        transmittance *= GetTransmittance(r, mu, outerLength, true);
        
        if (ai.intersectsInner)
        {
            vec3 p = ori + dir * ai.innerMin - planetLocation.xyz;
            float r = length(p);
            float mu = dot(dir, p) / r;
            float mu_s = dot(lightDir.xyz, p) / r;
            scattering -= transmittance * GetScattering(r, mu, mu_s, nu, intersectsGround);
        }
        color += max(vec3(0.0), scattering);
        // - to do: add *after* inner too if intersectsInner && intersectsOuter
    }
    // - to do: use outer atmosphere intersection separately, add precomputed scattering there
    
    if (ai.intersectsInner)
    {
        float tmin = ai.innerMin, tmax = ai.innerMax;
        solidDepth = min(solidDepth, tmax);
        
        /*solidDepth = min(solidDepth, 5e4); // - debugging
        backLight = vec3(0, 1, 0);*/
        
        // - intersecting the inner shell for optimising "inner" views (not that those would normally happen... right?)
        if (false)
        {
            float tmin2, tmax2;
            float R2 = planetRadius * 0.95;
            if (IntersectSphere(ori, dir, planetLocation.xyz, R2, tmin2, tmax2))
            {
                //if (false)
                //if (tmin2 == 0.0)
                {
                    //solidDepth = tmax2;
                    tmin = tmax2;
                }
            }
        }
        if (false)
        { // - debugging
            
            float dist = 0.0;
            
            float elevation = 0.5 + 1 * (cos(time) * 0.5 + 0.5);
            elevation = 0.0;
            float voxelSize = 15.6431 * 1e3;
            
            float R = planetRadius + (elevation * voxelSize);
            if (!IntersectSphere(ori, dir, planetLocation.xyz, R, tmin, tmax)) return;
            
            dist = tmin;
            
            float pz = 0.0;
            pz = 2.2e5; // - arbitrary
            if ((ori + dir * dist - planetLocation.xyz).z > pz)
            {
                vec3 pn = vec3(0, 0, 1);
                float pd = dot(-dir, pn);
                dist = dot(ori - (planetLocation.xyz + pn * pz), pn) / pd;
                if (length(ori + dir * dist - planetLocation.xyz) > R) return;
            }
            
            vec3 hit = ori + dir * dist - planetLocation.xyz;
            OctreeTraversalData o;
            o.p = hit / atmScale;
            OctreeDescendMap(o);
            o.center *= atmScale;
            o.size *= atmScale;
            const vec3 brickOffs = vec3(BrickIndexTo3D(o.ni));
            vec3 lc = (hit - o.center) / o.size;
            
            vec3 tc = lc * 0.5 + 0.5;
            vec3 ltc = tc;
            tc = BrickSampleCoordinates(brickOffs, tc);
            vec3 storedLight = vec3(texture(brickTexture, tc).r);
            
            tc *= vec3(textureSize(brickTexture, 0));
            //storedLight = vec3(texelFetch(brickTexture, ivec3(tc), 0).r);
            //if (false) // grid lines
            {
                tc = (lc * 0.5 + 0.5) * 7.0;
                vec3 gridLine = abs(fract(tc) - vec3(0.5));
                if (any(greaterThan(gridLine, vec3(0.48))))
                {
                    float maxLine = max(gridLine.x, max(gridLine.y, gridLine.z));
                    vec3 gridColor = vec3(1.0) - storedLight;
                    gridColor = vec3(0, 0, 1);
                    //storedLight = mix(storedLight, gridColor, smoothstep(0.48, 0.5, maxLine));
                    storedLight += gridColor * 0.1;
                    storedLight.b = max(0.0, storedLight.b) + 0.1;
                    
                    if (any(greaterThan(abs(ltc - vec3(0.5)), vec3(0.48))))
                    {
                        storedLight.g = max(0.0, storedLight.g) + 0.1;
                    }
                }
            }
            
            //storedLight = max(vec3(0.0), storedLight);
            //storedLight = min(vec3(1.0), storedLight);
            if (false)
            {
                if (any(greaterThan(storedLight, vec3(1.0)))) storedLight = vec3(0, 1, 0);
                if (any(lessThan(storedLight, vec3(0.0)))) storedLight = vec3(1, 0, 1);
            }
            //if (depth == 6u) storedLight.b = 1.0;
            
            //outValue = vec4(fragCoords / 1024.0, 0.0, 1.0);
            //storedLight = exp(-(1e2 * storedLight)); // - to do: scaling
            //if (any(lessThan(storedLight, vec3(0.0)))) storedLight = vec3(1, 0, 0);
            //if (any(greaterThan(storedLight, vec3(1.0)))) storedLight = vec3(0, 1, 0);
            
            storedLight *= 0.25;
            //outValue = vec4(storedLight, 1.0);
            return;
        }
        // - Debug mode for cross section of actual atmosphere:
        //if (false)
        {
            // - to do
        }
        
        const float outerMin = tmin;
        const vec3 hit = ori + dir * tmin - planetLocation.xyz;
        
        // - to do: find a way to not make clouds too dark/noisy without extreme numbers of steps
        // (maybe try to adaptively decrease step size at cloud boundaries?)
        
        const float stepFactor = 0.2;//0.1; // - arbitrary factor (to-be-tuned)
            stepSize;
        
        const vec3 globalStart = hit;
        OctreeTraversalData o;
        o.p = globalStart / atmScale;
        OctreeDescendMap(o);
        /*
        o.p = (o.p - mapPosition) / mapScale;
        OctreeDescendMap(frustumOctreeMap, o);
        */
        
        float dist = 0.0; // - to do: make it so this can be set to tmin?
        dist += 1e-5;// don't start at a face/edge/corner
        vec2 randTimeOffs = vec2(cos(time), sin(time));
        //randTimeOffs = vec2(0); // - testing
        // - experiment (should the factor be proportional to step size?)
        const float randOffs = (rand3D(vec3(fragCoords, randTimeOffs)) * 0.5 + 0.5) * 2 * stepFactor;
        dist += randOffs * o.size * atmScale;
        
        // - to do: think about whether randomness needs to be applied per-brick or just once
        // (large differences in depth could make the former necessary, no?)
        
        
        const uint maxSteps = 2048u; // - arbitrary, for testing
        
        const float mu = dot(lightDir.xyz, dir);
        const float phaseR = PhaseRayleigh(mu);
        const float phaseM = PhaseMie(mu);
        float lastRayleighDensity = 0.0, lastMieDensity = 0.0; // - or should these be the logarithms? Investigate
        
        // Trace through bricks:
        uint numBricks = 0u, numSteps = 0u;
        vec3 T = vec3(1.0);
        while (InvalidIndex != o.ni)
        {
            const bool isEmpty = (o.flags & EmptyBrickBit) != 0u;
            
            o.center *= atmScale;
            o.size *= atmScale;
            const float step = o.size / atmScale * stepFactor;
            const float atmStep = step * atmScale
                //* (float(isEmpty) + 1.0) // - experimental optimisation (causing banding, unfortunately)
                ;
            
            AabbIntersection(tmin, tmax, vec3(-o.size) + o.center, vec3(o.size) + o.center, hit, dir);
            tmax = min(tmax, solidDepth - outerMin);
            //if (isinf(tmin)) continue; // - testing
            
            //if (dist >= tmax) color += vec3(0.1); // - debugging
            //if (isinf(tmin) || isnan(tmin)) color += vec3(0.1); // - debugging
            
            const float randStep = randOffs * o.size;
            // - causing glitches. Hmm.
            //dist = ceil((tmin - randStep) / atmStep) * atmStep + randStep; // - try to avoid banding even in multi-LOD
            //dist += randOffs * o.size; // - to do: do this, but only when changing depth (or initially)?
            
            const vec3 brickOffs = vec3(BrickIndexTo3D(o.ni));
            vec3 localStart = (globalStart - o.center) / o.size;
            
            // Precomputed transmittance for start and end in node, to interpolate per-voxel:
            // - unfortunately interpolating these causes visible artefacts around the terminator
            const vec3 Tmin = TransmittanceFromPoint(hit + tmin * dir),
                       Tmax = TransmittanceFromPoint(hit + tmax * dir);

            // - to do: add random offset here (again), or only on depth change? Let's see
            
            float startDist = dist;
            int numStepsInNode = int(max(1.0, (tmax - dist) / atmStep));
            // Iterating at least once is required to avoid returning-to-the-same-node edge cases,
            // so a do-while loop is practical.
            //while (dist < tmax)
            do
            //for (int localStep = 1; localStep <= numStepsInNode;)// ++localStep)
            //for (int inner = 0; inner < 2; ++inner, ++localStep)
            {
                vec3 lc = localStart + dist / o.size * dir;
                vec3 tc = lc * 0.5 + 0.5;
                tc = clamp(tc, vec3(0.0), vec3(1.0)); // - should this really be needed? Currently there can be artefacts without this
                tc = BrickSampleCoordinates(brickOffs, tc);
                
                const bool animBlend = true;//false; // - roughly +100% slowdown in common/heavier cases
                const float animAlpha = 0.5; // - to do: time-based
                
                vec4 voxelData = texture(nextBrickTexture, tc);
                if (animBlend) voxelData = mix(texture(brickTexture, tc), voxelData, animationAlpha);
                
                
                vec3 storedLight = voxelData.yyy;
                
                vec3 p = hit + dist * dir;
                float r = length(p);
                float mu = dot(p / r, normalize(lightDir.xyz)); // - about 0.3 ms faster *with* the unnecessary lightDir.xyz normalisation. Hmm...
                
                // (too simple - the phase function needs to be accounted for too, separately for the more indirect lighting)
                //storedLight = max(vec3(0.2), storedLight); // - experimental: don't make clouds entirely dark
                
                //storedLight *= GetTransmittanceToSun(r, mu); // - heavy: 1/4 in some scenes
                //storedLight *= mix(Tmin, Tmax, (dist - tmin) / (tmax - tmin)); // - experimental
                // - faster without sun occlusion computation: (but artefacts, somewhere, maybe?)
                storedLight *= GetTransmittanceToAtmosphereTop(r, mu); // - still quite expensive
                
                // - computing base Rayleigh and Mie densities does take some time (maybe 1/6 to 1/4)
                // Maybe see if using a (very low-resolution) lookup texture helps?
                // (less time in heavy scenes, it seems)
                
                float rayleighDensity = 0.0, mieDensity = 0.0;
                ComputeBaseDensities(rayleighDensity, mieDensity, r);
                mieDensity += voxelData.x * mieMul; // - could work now that the data has been simplified
                max(0.0, (voxelData.x * scaleM + offsetM) * mieMul); // - testing (this non-exponential (linear) interpolation preserves interesting shapes much better. Hmm.)
                
                T = transmittance * exp(-(opticalDepthR * betaR.xyz + opticalDepthM * betaMEx));
                color += (phaseR * betaR.xyz * rayleighDensity + 
                    //6 * // - testing (this brightens the clouds to great improvement. Must be tuned, somehow)
                    phaseM * betaMSca * mieDensity) 
                    * T * storedLight * atmStep;
                    
                // - experiment: Mie added to optical depth *after*, to not occlude itself
                // (should also be the case for Rayleigh, maybe?)
                // - but adding Mie optical depth here tends to make clouds overly bright at close range
                // (which might turn out to not be a problem with adaptive loading of higher detail, eventually)
                
                const bool midPoint = false;//true;
                if (midPoint)
                {
                    opticalDepthR += rayleighDensity * atmStep;
                    opticalDepthM += mieDensity * atmStep;
                }
                else // trapezoidal
                {
                    opticalDepthR += (lastRayleighDensity + rayleighDensity) * 0.5 * atmStep;
                    opticalDepthM += (lastMieDensity + mieDensity) * 0.5 * atmStep;
                }
                
                lastRayleighDensity = rayleighDensity;
                lastMieDensity = lastMieDensity;
                
                dist += atmStep; // - for while loops
                //dist = startDist + atmStep * float(localStep); // - for for loops
                ++numSteps;
                
                //if (length(T) < 1e-3) break; // stop early if transmittance is low // - too slow to have in here
            } while (dist < tmax);
            ++numBricks;
            
            // - used to have 1e-3, but that was high enough to visible show the node grid lines. 1e-4 works.
            // (maybe try to keep a higher threshold in directions not pointing towards the sun? To do)
            if (length(T) < 1e-4) break; // stop early if transmittance is low
            
            //dist = tmax + 1e-4; // - testing (but this is dangerous. To do: better epsilon)
            
            if (dist + outerMin > solidDepth) break;
            const uint old = o.ni;
            vec3 p = (hit + dist * dir) / atmScale;
            
            o.p = p;
            OctreeDescendMap(o);
            
            // - using a map fit for the frustum:
            // (this turned out to only give a very small improvement - perhaps roughly 1/30 of render time in the better cases)
            //o.p = (o.p - mapPosition) / mapScale;
            //OctreeDescendMap(frustumOctreeMap, o);
            
            // - not necessary now that the loop above is a do-while
            /*if (old == o.ni)
            {
                dist += atmStep; // - testing. Seems like this works wonders vs the black spots, but maybe not *entirely* eliminates them
                //color.r += 1.0; // - debugging (this covers all black spots, it seems. Bingo. But why does this happen erroneously?)
                //break; // - error (but can this even happen?) (Yes, it does happen. Quite often and early, interestingly enough)
            }*/
            
            // - not checking this gives a small improvement (measured approx. 0.6 ms down from 14.6 ms in one case)
            //if (numBricks >= 128u || numSteps >= maxSteps) break; // - testing
            //if (numSteps > 400u) break; // - testing
        }
        
        { // - debug visualisation
            //if (numSteps > 500) color.r = 0.1;
            //if (numBricks > 80) color.g = 0.0;
            //color.r += 0.003 * float(numBricks);
            //if (0u == (numSteps & 1u)) color.r += 0.1;
        }
        
        if (AddSecondOuter)
        if (ai.intersectsOuter && (!ai.intersectsInner || ai.innerMax < actualSolidDepth))
        {
            // - to do: handle depth buffer value possibly interrupting this
            vec3 p = ori + dir * ai.innerMax - planetLocation.xyz;
            float r = length(p);
            float mu = dot(dir, p) / r;
            float mu_s = dot(lightDir.xyz, p) / r;
            float nu = dot(dir, lightDir.xyz);
            vec3 T = transmittance * exp(-(opticalDepthR * betaR.xyz + opticalDepthM * betaMEx));
            color += T * GetScattering(r, mu, mu_s, nu, false);
            // - to do: transmittance, also considering potentially occluding depth buffer value
            //transmittance *= GetTransmittance(r, mu, outerLength, true);
        }
    }
    
    if (false)
    { // - debugging
        ivec3 size = textureSize(scatterTexture, 0);
        ivec2 ic = ivec2(fragCoords);
        const int cols = 4;
        int z = ic.y / size.y * cols + ic.x / size.x;
        if (z < size.z && ic.x / size.x < cols)
        {
            ic.x = ic.x % size.x;
            ic.y = ic.y % size.y;
            color = vec3(texelFetch(scatterTexture, ivec3(ic, z), 0));
        }
        //color = vec3(texelFetch(transmittanceTexture, ivec2(fragCoords), 0));
    }
    const vec3 lightIntensity = vec3(1.0) * sun.z;
    color *= lightIntensity;
    
    transmittance *= exp(-(opticalDepthR * betaR.xyz + opticalDepthM * betaMEx));
    color *= 3.0; // - testing (to do: tune light intensity instead)
    //color += backLight * transmittance;
    //color = vec3(1.0); // - testing
    imageStore(lightImage, ifragCoords, vec4(color, 0.0));
    imageStore(transmittanceImage, ifragCoords, vec4(transmittance, 0.0));
}
