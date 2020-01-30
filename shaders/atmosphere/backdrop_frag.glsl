#version 450

#include "common.glsl"
#include "../geometry.glsl"
layout(location = 0) out vec4 outValue;
in vec4 ndc;
//in float flogz;

void main()
{
    const vec3 ori = vec3(invViewMat * vec4(0, 0, 0, 1));
    const vec3 dir = normalize(vec3(invViewProjMat * ndc));
    
    const vec3 center = planetLocation;
    float t0, t1;
    if (!IntersectSphere(ori, dir, center, planetRadius, t0, t1)) discard;
    if (t0 <= 0.0) discard; // don't block if we're inside the planet (since it's fun to look out into the atmosphere shell)
    vec3 hitp = ori + t0 * dir;
    vec3 normal = normalize(hitp - center);
    
    vec3 color = vec3(max(0.0, dot(normal, lightDir))); // Lambertian
    vec3 diffuseColor = vec3(1.0);
    diffuseColor = vec3(0.01, 0.05, 0.1);
    diffuseColor = pow(vec3(0.016, 0.306, 0.482), vec3(2.2));
    color *= diffuseColor;
    //color = vec3(0.0);
    
    { // - testing modulation by atmosphere lighting
        // - the sample position probably needs to be dithered in time... no?
        vec3 p = (hitp - center) / atmosphereScale / planetRadius;
        vec3 nodeCenter;
        float nodeSize;
        uint depth;
        uint ni = OctreeDescend(p, nodeCenter, nodeSize, depth);
        if (ni != InvalidIndex)
        {
            const vec3 brickOffs = vec3(BrickIndexTo3D(ni));
            vec3 lc = (p - nodeCenter) / nodeSize;
            vec3 tc = lc * 0.5 + 0.5;
            tc = clamp(tc, vec3(0.0), vec3(1.0));
            tc = BrickSampleCoordinates(brickOffs, tc);
            
            vec3 storedLight = max(vec3(texture(brickLightTexture, tc)), vec3(0.0));
            color *= storedLight;
        }
    }
    
    outValue = vec4(color, 1);
    //float flogz = 1.0 + (worldViewProjMat * vec4(hitp, 1.0)).w;
    float flogz = 1.0 + (viewProjMat * vec4(hitp, 1.0)).w;
    gl_FragDepth = log2(flogz) * Fcoef_half;
}
