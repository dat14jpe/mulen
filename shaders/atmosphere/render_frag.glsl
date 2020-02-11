#version 450
#include "common.glsl"
#include "../geometry.glsl"
#include "../noise.glsl"

layout(location = 0) out vec4 outValue;
in vec4 clip_coords;
uniform layout(binding=4) sampler2D lightTexture;


float GetDepth()
{
    float d = texture(depthTexture, clip_coords.xy * 0.5 + 0.5).x;
    d = exp2(d / Fcoef_half) - 1.0;
    return d;
}

vec3 ViewspaceFromDepth(float depth)
{
    vec4 cs = vec4(clip_coords.xy * depth, -depth, depth);
    vec4 vs = invProjMat * cs;
    return vs.xyz;
}

void main()
{
    vec3 backLight = texelFetch(lightTexture, ivec2(gl_FragCoord.xy), 0).xyz;
    const float atmScale = atmosphereRadius;
    
    const vec3 ori = vec3(invViewMat * vec4(0, 0, 0, 1));
    vec3 dir = normalize(vec3(invViewProjMat * clip_coords));
    float solidDepth = length(ViewspaceFromDepth(GetDepth()));
    
    float opticalDepthR = 0.0, opticalDepthM = 0.0;
    vec3 transmittance = vec3(1.0);
    vec3 color = vec3(0.0);
    
    float tmin, tmax;
    const float atmFactor = 1.7;//1.1; // - to do: adjust so that the upper "Rayleigh line" is not visible
    float R = planetRadius + atmosphereHeight * atmFactor;
    if (IntersectSphere(ori, dir, planetLocation, R, tmin, tmax))
    {
        solidDepth = min(solidDepth, tmax);
        
        /*solidDepth = min(solidDepth, 5e4); // - debugging
        backLight = vec3(0, 1, 0);*/
        
        // - intersecting the inner shell for optimising "inner" views (not that those would normally happen... right?)
        if (false)
        {
            float tmin2, tmax2;
            float R2 = planetRadius * 0.95;
            if (IntersectSphere(ori, dir, planetLocation, R2, tmin2, tmax2))
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
            if (!IntersectSphere(ori, dir, planetLocation, R, tmin, tmax)) discard;
            
            dist = tmin;
            
            float pz = 0.0;
            pz = 2.2e5; // - arbitrary
            if ((ori + dir * dist - planetLocation).z > pz)
            {
                vec3 pn = vec3(0, 0, 1);
                float pd = dot(-dir, pn);
                dist = dot(ori - (planetLocation + pn * pz), pn) / pd;
                if (length(ori + dir * dist - planetLocation) > R) discard;
            }
            
            vec3 hit = ori + dir * dist - planetLocation;
            uint depth;
            vec3 nodeCenter;
            float nodeSize;
            uint ni = OctreeDescendMap(hit / atmScale, nodeCenter, nodeSize, depth);
            nodeCenter *= atmScale;
            nodeSize *= atmScale;
            const vec3 brickOffs = vec3(BrickIndexTo3D(ni));
            vec3 lc = (hit - nodeCenter) / nodeSize;
            
            vec3 tc = lc * 0.5 + 0.5;
            vec3 ltc = tc;
            tc = BrickSampleCoordinates(brickOffs, tc);
            vec3 storedLight = vec3(texture(brickLightTexture, tc));
            
            tc *= vec3(textureSize(brickLightTexture, 0));
            //storedLight = vec3(texelFetch(brickLightTexture, ivec3(tc), 0));
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
            
            //outValue = vec4(gl_FragCoord.xy / 1024.0, 0.0, 1.0);
            //storedLight = exp(-(1e2 * storedLight)); // - to do: scaling
            //if (any(lessThan(storedLight, vec3(0.0)))) storedLight = vec3(1, 0, 0);
            //if (any(greaterThan(storedLight, vec3(1.0)))) storedLight = vec3(0, 1, 0);
            
            storedLight *= 0.25;
            outValue = vec4(storedLight, 1.0);
            return;
        }
        // - Debug mode for cross section of actual atmosphere:
        //if (false)
        {
            // - to do: do this
        }
        
        const float outerMin = tmin;
        const vec3 hit = ori + dir * tmin - planetLocation;
        
        // - to do: find a way to not make clouds too dark/noisy without extreme numbers of steps
        // (maybe try to adaptively decrease step size at cloud boundaries?)
        
        const float stepFactor = 0.1; // - arbitrary factor (to-be-tuned)
            stepSize;
        
        const vec3 globalStart = hit;
        uint depth;
        vec3 nodeCenter;
        float nodeSize;
        uint ni = OctreeDescendMap(globalStart / atmScale, nodeCenter, nodeSize, depth);
        float dist = 0.0; // - to do: make it so this can be set to tmin?
        dist += 1e-5;// don't start at a face/edge/corner
        vec2 randTimeOffs = vec2(cos(time), sin(time));
        //randTimeOffs = vec2(0); // - testing
        // - experiment (should the factor be proportional to step size?)
        const float randOffs = (rand3D(vec3(gl_FragCoord.xy, randTimeOffs)) * 0.5 + 0.5) * 2 * stepFactor;
        dist += randOffs * nodeSize * atmScale;
        
        // - to do: think about whether randomness needs to be applied per-brick or just once
        // (large differences in depth could make the former necessary, no?)
        
        
        const uint maxSteps = 1024u; // - arbitrary, for testing
        
        const float mu = dot(lightDir, dir);
        const float phaseR = PhaseRayleigh(mu);
        const float phaseM = PhaseMie(mu);
        float lastRayleighDensity = 0.0, lastMieDensity = 0.0; // - or should these be the logarithms? Investigate
        
        // Trace through bricks:
        uint numBricks = 0u, numSteps = 0u;
        while (InvalidIndex != ni)
        {   
            nodeCenter *= atmScale;
            nodeSize *= atmScale;
            const float step = nodeSize / atmScale * stepFactor;
            const float atmStep = step * atmScale;
            
            AabbIntersection(tmin, tmax, vec3(-nodeSize) + nodeCenter, vec3(nodeSize) + nodeCenter, hit, dir);
            tmax = min(tmax, solidDepth - outerMin);
            //if (isinf(tmin)) continue; // - testing
            
            const float randStep = randOffs * nodeSize;
            // - causing glitches. Hmm.
            //dist = ceil((tmin - randStep) / atmStep) * atmStep + randStep; // - try to avoid banding even in multi-LOD
            //dist += randOffs * nodeSize; // - to do: do this, but only when changing depth (or initially)?
            
            const vec3 brickOffs = vec3(BrickIndexTo3D(ni));
            vec3 localStart = (globalStart - nodeCenter) / nodeSize;
            vec3 lc = localStart + dist / nodeSize * dir;

            // - to do: add random offset here (again), or only on depth change? Let's see
            
            while (dist < tmax && numSteps < maxSteps)
            {            
                vec3 tc = lc * 0.5 + 0.5;
                tc = clamp(tc, vec3(0.0), vec3(1.0)); // - should this really be needed? Currently there can be artefacts without this
                tc = BrickSampleCoordinates(brickOffs, tc);
                
                vec3 storedLight = max(vec3(texture(brickLightTexture, tc)), vec3(0.0));
                storedLight = min(vec3(1.0), storedLight); // for interpolation, lighting shadows can be "negative" (but is this needed?)
                //storedLight = vec3(1.0); // - debugging
                
                vec3 p = hit + dist * dir;
                float r = length(p);
                float mu = dot(p / r, normalize(lightDir)); // - about 0.3 ms faster *with* the unnecessary lightDir normalisation. Hmm...
                storedLight = storedLight.xxx; // - problem since removing Rayleigh channel. Look into lighting
                storedLight *= GetTransmittanceToSun(r, mu);
                
                
                if (false)
                { // - debugging: try a more theoretical transmittance computation, to see if that gives workable results
                    // (it does. What does that tell us? Per-step computations work? Well, yes, but the expense is prohibitive...)
                    float ldist = 0.0;
                    vec3 lori = hit + dist * dir;
                    float opticalDepthR = 0.0;
                    const uint numSteps = 128u;
                    float atmStep = 1e4;
                    //lori = normalize(lori) * planetRadius; // - testing // - yes, it's smooth. But how to do that with varying radius?
                    for (uint i = 0u; i < numSteps; ++i)
                    {
                        vec3 p = lori + ldist * lightDir;
                        float densityR = exp(-(length(p) - planetRadius) / 8e3);
                        
                        opticalDepthR += densityR * atmStep;
                        ldist += atmStep;
                    }
                    storedLight = exp(-betaR * opticalDepthR);
                }
                
                vec4 voxelData = texture(brickTexture, tc);
                
                float rayleigh = voxelData.y, mie = voxelData.x;
                float rayleighDensity = RayleighDensityFromSample(rayleigh);
                // - to do: compute Rayleigh density theoretically
                rayleighDensity = exp(-(r - Rg) / HR);
                float mieDensity = MieDensityFromSample(mie);
                //mieDensity = mie * 0.02; // - testing (this non-exponential (linear) interpolation preserves interesting shapes much better. Hmm.)
                
                
                { // - test: hardcoded upper cloud boundary
                    // - to do: apply in lighting too, if it's going to be used
                    // It seems like this *might* get rid of the structural banding at low resolution.
                    // But the performance cost is about 10% (or more?). Hmm.
                    // (and the flexibility cost is steep as well: no mist or low clouds in general)
                    vec3 p = (hit + dir * dist) / planetRadius;
                    const float atmHeight = 0.01;
                    const float relativeCloudTop = 0.4; // - to do: tune
                    //mieDensity *= 1.0 - smoothstep(relativeCloudTop * 0.75, relativeCloudTop, (length(p) - 1.0) / atmHeight);
                    float bottom = smoothstep(1.0005, 1.001, length(p));
                    float a = 1.0 / 6371.0;
                    //bottom = max(bottom, 1.0 - smoothstep(1.0 + a * 0.5, 1.0 + a, length(p)));
                    //mieDensity *= bottom; // - testing bottom hardcode as well (should be avoided)
                }
                
                // - seems like high Mie (i.e. clouds) is extinguishing itself. Whoops. How to fix without horrible flickering?
                
                const vec3 lightIntensity = vec3(1.0) * sun.z;
                //mieDensity *= 30.0; // - debugging
                transmittance = exp(-(opticalDepthR * betaR + opticalDepthM * betaMEx));
                color += (phaseR * betaR * rayleighDensity + phaseM * betaMSca * mieDensity) 
                    * transmittance * storedLight * lightIntensity * atmStep;
                //color += /*transmittance * */storedLight * atmStep * 1e-6; // - debugging
                    
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
                
                dist += atmStep;
                lc = localStart + dist / nodeSize * dir;
                ++numSteps;
            }
            ++numBricks;
            
            // This optimisation seems highly effective (20200204).
            // Near-doubling in many cases, and possibly more in others.
            if (length(transmittance) < 1e-3) break;
            
            //dist = tmax + 1e-4; // - testing (but this is dangerous. To do: better epsilon)
            
            const uint old = ni;
            vec3 p = (hit + dist * dir) / atmScale;
            if (dist + outerMin > solidDepth) break;
            if (any(greaterThan(abs(p), vec3(1.0)))) break; // - outside
            ni = OctreeDescendMap(p, nodeCenter, nodeSize, depth);
            if (old == ni) break; // - error (but can this even happen?)
            
            if (numBricks >= 64u) break; // - testing
        }
        
        { // - debug visualisation
            //if (numSteps > 30) color.r = 1.0;
            //if (numBricks > 20) color.g = 0.0;
            //color.r += 0.02 * float(numBricks);
            //if (0u == (numSteps & 1u)) color.r += 0.1;
        }
    }
    
    transmittance = exp(-(opticalDepthR * betaR + opticalDepthM * betaMEx));
    color += backLight * transmittance;
    outValue = vec4(color, 1.0);
}
