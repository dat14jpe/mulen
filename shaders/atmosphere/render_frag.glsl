#version 450
#include "common.glsl"
#include "../geometry.glsl"

layout(location = 0) out vec4 outValue;
in vec4 clip_coords;


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
    const float atmScale = atmosphereRadius;
    
    const vec3 ori = vec3(invViewMat * vec4(0, 0, 0, 1));
    vec3 dir = normalize(vec3(invViewProjMat * clip_coords));
    float solidDepth = length(ViewspaceFromDepth(GetDepth()));
    
    float tmin, tmax;
    float R = planetRadius + atmosphereHeight;
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
    
    const float stepFactor = 0.2 * stepSize;//0.1; // - arbitrary factor (to-be-tuned)
    
    const vec3 globalStart = hit;
    uint depth;
    vec3 nodeCenter;
    float nodeSize;
    uint ni = OctreeDescend(globalStart / atmScale, nodeCenter, nodeSize, depth);
    float dist = 0.0; // - to do: make it so this can be set to tmin?
    dist += 1e-5;// don't start at a face/edge/corner
    vec2 randTimeOffs = vec2(cos(time), sin(time));
    //randTimeOffs = vec2(0); // - testing
    // - experiment (should the factor be proportional to step size?)
    const float randOffs = (random(gl_FragCoord.xy + randTimeOffs) * 0.5 + 0.5) * 2 * stepFactor;
    dist += randOffs * nodeSize * atmScale;
    
    // - to do: think about whether randomness needs to be applied per-brick or just once
    // (large differences in depth could make the former necessary, no?)
    
    
    const uint maxSteps = 512u; // - arbitrary, for testing
    
    float alpha = 0.0;
    
    //if (ni > 7u) discard; // - testing. Strangely low nodes used (while splitting 4 levels)
    
    // Trace through bricks:
    uint numBricks = 0u, numSteps = 0u;
    while (InvalidIndex != ni)
    {
        // - debugging (structure visualisation)
        if (false)
        {
            if (depth < 6)
            {
                alpha += 0.05;
                color += vec3(1, 0, 0) * 1e-2;
                if (depth == 1u) color += vec3(0, 1, 0) * 1e-2;
                if (depth == 2u) color += vec3(0, 0, 1) * 1e-2;
            }
            if (depth == 6) { alpha += 0.05; color += vec3(0, 1, 0) * 1e-3; }
        }
        
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
            if (alpha > 0.999) break; // - to do: tune
            
            vec3 tc = lc * 0.5 + 0.5;
            tc = clamp(tc, vec3(0.0), vec3(1.0)); // - should this really be needed? Currently there can be artefacts without this
            tc = BrickSampleCoordinates(brickOffs, tc);
            vec4 voxelData = texture(brickTexture, tc);
            
            float density = voxelData.x; // - to do: threshold correctly, as if distance field
            //density *= 4.0; // - more normal
            //density = 0.4; // - good for testing "light shadows"
            density *= 200.0; // - testing
            //density = smoothstep(0.1, 0.75, density); // - testing
            const float visibility = 1.0 - alpha;
            vec3 newLight = vec3(1.0);
            newLight *= visibility * density * step;
            alpha += visibility * density * step; // - to do: do this correctly, not ad hoc
            newLight *= max(vec3(texture(brickLightTexture, tc)), vec3(0.0));
            color += newLight;
            
            dist += atmStep;
            lc = localStart + dist / nodeSize * dir;
            ++numSteps;
        }
        ++numBricks;
        //dist = tmax + 1e-4; // - testing (but this is dangerous. To do: better epsilon)
        
        // - to do: try traversal via neighbours, possibly going down/up one level
        // (need to pass through 1-3 neighbours here)
        
        const uint old = ni;
        vec3 p = (hit + dist * dir) / atmScale;
        if (dist + outerMin > solidDepth) break;
        if (any(greaterThan(abs(p), vec3(1.0)))) break; // - outside
        ni = OctreeDescend(p, nodeCenter, nodeSize, depth);
        //if (old == ni) break; // - error (but can this even happen?)
        
        if (numBricks >= 64u) break; // - testing
    }
    //if (numSteps > 30) { color.r = 1.0; alpha = max(alpha, 0.5); } // - performance visualisation
    //if (numBricks > 0) { color.g = 1.0; alpha = max(alpha, 0.5); } // - performance visualisation
    
    outValue = vec4(color * alpha, min(1.0, alpha));
}
