#version 450
#include "common.glsl"

layout(location = 0) out vec4 outValue;
in vec4 clip_coords;
uniform mat4 invWorldViewMat, invWorldViewProjMat, invViewProjMat;
uniform uint rootGroupIndex;
uniform sampler3D brickTexture;
uniform float time;
uniform sampler2D depthTexture;
uniform float Fcoef_half;
    
vec3 boxHitToNormal(vec3 ori, vec3 dir, float t)
{
    vec3 normal = ori + dir * t;
    const vec3 an = abs(normal);
    if (an.x > an.y && an.x > an.z) normal = vec3(sign(normal.x), 0, 0);
    else if (an.y > an.z) normal = vec3(0, sign(normal.y), 0);
    else normal = vec3(0, 0, sign(normal.z));
    return normal;
}

// - to do: add "desired size" parameter to allow early stop
// p components in [-1, 1] range
uint OctreeDescend(vec3 p, out vec3 nodeCenter, out float nodeSize)
{
    uint ni = InvalidIndex;
    uint gi = rootGroupIndex;
    vec3 center = vec3(0.0);
    float size = 1.0;
    while (InvalidIndex != gi)
    {
        ivec3 ioffs = clamp(ivec3(p + 1.0), ivec3(0), ivec3(1));
        uint child = uint(ioffs.x) + uint(ioffs.y) * 2u + uint(ioffs.z) * 4u;
        ni = gi * NodeArity + child;
        size *= 0.5;
        center += (vec3(ioffs) * 2.0 - 1.0) * size;
        gi = nodeGroups[gi].nodes[child].children;
    }
    nodeCenter = center;
    nodeSize = size;
    return ni;
}

float GetDepth()
{
    float d = texture(depthTexture, clip_coords.xy * 0.5 + 0.5).x;
    d = exp2(d / Fcoef_half) - 1.0;
    return d;
}

void main()
{
    const vec3 lightDir = normalize(vec3(1, 0.6, 0.4));
    
    const vec3 ori = vec3(invWorldViewMat * vec4(0, 0, 0, 1));
    vec3 dir = normalize(vec3(invWorldViewProjMat * clip_coords));
    
    const float depth = GetDepth();
    vec4 depthPos4 = invViewProjMat * vec4(clip_coords.xy, depth, 1.0);
    vec3 ddir = depthPos4.xyz / depthPos4.w;
    const float distance = length(depth);
    const float solidDepth = length(ddir); // - to do: transform to object space
    //outValue = vec4(vec3(depth), 1.0);
    //if (distance > 2.01) discard; // - debugging
    
    #ifndef Testing
    
    vec3 diffuseColor = vec3(0.0);
    
    float tmin, tmax;
    AabbIntersection(tmin, tmax, vec3(-1), vec3(1), ori, dir);
    if (tmin < 0.0 && tmax > 0.0) tmin = 0.0; // ray starting inside the box
    if (!IsIntersection(tmin, tmax)) discard;
    const vec3 hit = ori + dir * tmin;
    
    const float stepFactor = 0.2;//0.1; // - arbitrary factor (to-be-tuned)
    
    const vec3 globalStart = hit;
    vec3 nodeCenter;
    float nodeSize;
    uint ni = OctreeDescend(globalStart, nodeCenter, nodeSize);
    vec3 localStart = (globalStart - nodeCenter) / nodeSize;
    float dist = 0.0;
    dist += 1e-5;// don't start at a face/edge/corner
    vec2 randTimeOffs = vec2(cos(time), sin(time));
    //randTimeOffs = vec2(0); // - testing
    dist += (random(gl_FragCoord.xy + randTimeOffs) * 0.5 + 0.5) * 2 * stepFactor * nodeSize; // - experiment (should the factor be proportional to step size?)
    
    // - to do: think about whether randomness needs to be applied per-brick or just once
    // (large differences in depth could make the former necessary, no?)
    
    
    float alpha = 0.0;
    //diffuseColor = globalStart * 0.5 + 0.5;
    {
        
        // - to do: trace through bricks
        while (InvalidIndex != ni)
        {
            const vec3 brickOffs = vec3(BrickIndexTo3D(ni));
            vec3 color = vec3(1.0);//localStart * 0.5 + 0.5;
            //diffuseColor += color * 0.0625;
            vec3 lc = localStart + dist / nodeSize * dir;
    
            // - to do: add random offset here (again), or only on depth change? Let's see
            
            // - to do: try calculating the end dist and then just iterating with that one float condition
            // (which would also simplify depth-testing)
            
            const float step = nodeSize * stepFactor;
            float count = 0;
            while (!any(greaterThan(abs(lc), vec3(1.0))))
            {
                if (dist > solidDepth) break;
                //count += 1.0; // - to-be-removed
                vec3 tc = BrickSampleCoordinates(brickOffs, lc * 0.5 + 0.5);
                vec4 voxelData = texture(brickTexture, tc);
                // - to do: use sample
                float density = voxelData.x;
                //if (density > 0.0) count += 2.0;
                count += density; // - to do: threshold correctly, as if distance field
                alpha += 0.5 * density; // - to do: do this correctly, not ad hoc
                
                dist += step;
                lc = localStart + dist / nodeSize * dir;
            } 
            diffuseColor += color * count / 64.0;
            
            // - to do: try traversal via neighbours, possibly going down/up one level
            // (need to pass through 1-3 neighbours here)
            
            const uint old = ni;
            vec3 p = hit + dist * dir;
            if (dist > solidDepth) break;
            if (any(greaterThan(abs(p), vec3(1.0)))) break; // - outside
            ni = OctreeDescend(hit + dist * dir, nodeCenter, nodeSize);
            localStart = (globalStart - nodeCenter) / nodeSize;
            if (old == ni) break; // - error (but can this even happen?)
        }
    }
    
    vec3 normal = boxHitToNormal(ori, dir, tmin);
    //normal = vec3(0, 0, 1); // - debugging
    
    vec3 color;// = normal * 0.5 + 0.5; // test
    color = vec3(max(0.0, dot(normal, lightDir))); // Lambertian
    color = vec3(1.0);
    //color = mix(color, vec3(max(0, dot(boxHitToNormal(ori, dir, tmax), lightDir))), 0.25);
    color *= diffuseColor;
    
    outValue = vec4(color, min(1.0, alpha));
    
    //outValue = vec4(dir * 0.5 + 0.5, 1.0);
    #endif
}
