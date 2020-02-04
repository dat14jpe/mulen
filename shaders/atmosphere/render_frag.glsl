#version 450
#include "common.glsl"
#include "../geometry.glsl"

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
    const vec3 backLight = texelFetch(lightTexture, ivec2(gl_FragCoord.xy), 0).xyz;
    const float atmScale = atmosphereRadius;
    
    const vec3 ori = vec3(invViewMat * vec4(0, 0, 0, 1));
    vec3 dir = normalize(vec3(invViewProjMat * clip_coords));
    float solidDepth = length(ViewspaceFromDepth(GetDepth()));
    
    float tmin, tmax;
    const float atmFactor = 1.1; // - to do: adjust so that the upper "Rayleigh line" is not visible
    float R = planetRadius + atmosphereHeight * atmFactor;
    if (!IntersectSphere(ori, dir, planetLocation, R, tmin, tmax)) discard;
    solidDepth = min(solidDepth, tmax);
    
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
    
    
    vec3 color = vec3(0.0);
    
    const float outerMin = tmin;
    const vec3 hit = ori + dir * tmin - planetLocation;
    
    // - to do: find a way to not make clouds too dark/noisy without extreme numbers of steps
    // (maybe try to adaptively decrease step size at cloud boundaries?)
    
    const float stepFactor = 0.1 * //0.1; // - arbitrary factor (to-be-tuned)
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
    
    float opticalDepthR = 0.0, opticalDepthM = 0.0;
    vec3 transmittance = vec3(1.0);
    const float mu = dot(lightDir, dir);
    const float phaseR = PhaseRayleigh(mu);
    const float phaseM = PhaseMie(mu);
    
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
            vec4 voxelData = texture(brickTexture, tc);
            
            float rayleigh = voxelData.x, mie = voxelData.y;
            float rayleighDensity = RayleighDensityFromSample(rayleigh);
            float mieDensity = MieDensityFromSample(mie);
            
            
            { // - test: hardcoded upper cloud boundary
                // - to do: apply in lighting too, if it's going to be used
                // It seems like this *might* get rid of the structural banding at low resolution.
                // But the performance cost is about 10% (or more?). Hmm.
                // (and the flexibility cost is steep as well: no mist or low clouds in general)
                vec3 p = (hit + dir * dist) / planetRadius;
                const float atmHeight = 0.01;
                const float relativeCloudTop = 0.4; // - to do: tune
                mieDensity *= 1.0 - smoothstep(relativeCloudTop * 0.75, relativeCloudTop, (length(p) - 1.0) / atmHeight);
                float bottom = smoothstep(1.0005, 1.001, length(p));
                float a = 1.0 / 6371.0;
                //bottom = max(bottom, 1.0 - smoothstep(1.0 + a * 0.5, 1.0 + a, length(p)));
                mieDensity *= bottom; // - testing bottom hardcode as well (should be avoided)
            }
            
            // - seems like high Mie (i.e. clouds) is extinguishing itself. Whoops. How to fix without horrible flickering?
            
            const vec3 lightIntensity = vec3(5e0); // - to do: uniform
            //mieDensity *= 30.0; // - debugging
            transmittance = exp(-(opticalDepthR * betaR + opticalDepthM * betaMEx));
            color += (phaseR * betaR * rayleighDensity + phaseM * betaMSca * mieDensity) 
                * transmittance * storedLight * lightIntensity * atmStep;
            // - experiment: Mie added to optical depth *after*, to not occlude itself
            // (should also be the case for Rayleigh, maybe?)
            // - but adding Mie optical depth here tends to make clouds overly bright at close range
            // (which might turn out to not be a problem with adaptive loading of higher detail, eventually)
            opticalDepthR += rayleighDensity * atmStep;
            opticalDepthM += mieDensity * atmStep;
            
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
    //if (numSteps > 30) { color.r = 1.0; alpha = max(alpha, 0.5); } // - performance visualisation
    //if (numBricks > 20) { color.g = 1.0; alpha = max(alpha, 0.5); } // - performance visualisation
    
    color += backLight * transmittance;
    outValue = vec4(color, 1.0);
}
