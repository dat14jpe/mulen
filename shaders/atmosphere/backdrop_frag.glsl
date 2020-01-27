#version 450

#include "../geometry.glsl"
layout(location = 0) out vec4 outValue;
in vec4 ndc;
//in float flogz;
uniform mat4 invWorldViewMat, invWorldViewProjMat, worldViewProjMat;
uniform float Fcoef_half;
uniform float planetRadius;

void main()
{
    const vec3 lightDir = normalize(vec3(1, 1, 1));
    
    const vec3 ori = vec3(invWorldViewMat * vec4(0, 0, 0, 1));
    const vec3 dir = normalize(vec3(invWorldViewProjMat * ndc));
    
    const vec3 center = vec3(0.0);
    float t0, t1;
    if (!IntersectSphere(ori, dir, center, planetRadius, t0, t1)) discard;
    if (t0 <= 0.0) discard; // don't block if we're inside the planet (since it's fun to look out into the atmosphere shell)
    vec3 hitp = ori + t0 * dir;
    vec3 normal = normalize(hitp - center);
    
    vec3 color = normal * 0.5 + 0.5; // test
    color = vec3(max(0.0, dot(normal, lightDir))); // Lambertian
    vec3 diffuseColor = vec3(1.0);
    diffuseColor = vec3(0.01, 0.05, 0.1); // - testing
    color *= diffuseColor;
    
    outValue = vec4(color, 1);
    float flogz = 1.0 + (worldViewProjMat * vec4(hitp, 1.0)).w;
    gl_FragDepth = log2(flogz) * Fcoef_half;
}
