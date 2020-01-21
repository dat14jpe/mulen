#version 450

layout(location = 0) out vec4 outValue;
in vec4 ndc;
//in float flogz;
uniform mat4 invWorldViewMat, invWorldViewProjMat, worldViewProjMat;
uniform float Fcoef_half;

void main()
{
    const vec3 lightDir = normalize(vec3(1, 1, 1));
    
    const vec3 ori = vec3(invWorldViewMat * vec4(0, 0, 0, 1));
    const vec3 dir = normalize(vec3(invWorldViewProjMat * ndc));
    
    const vec3 center = vec3(0, 0, 0);
    const float radius = 1.0;
    const float radius2 = radius*radius;
    
    float a = dot(dir, dir);
    float b = 2.0 * dot(dir, ori - center);
    float c = dot(center, center) + dot(ori, ori) - 2.0 * dot(center, ori) - radius2;
    float test = b*b - 4.0*a*c;

    if (test < 0.0) discard; // miss
    float u = (-b - sqrt(test)) / (2.0 * a);
    if (u < 0.0) discard; // miss
    vec3 hitp = ori + u * dir;
    vec3 normal = normalize(hitp - center);
    
    vec3 color = normal * 0.5 + 0.5; // test
    color = vec3(max(0.0, dot(normal, lightDir))); // Lambertian
    vec3 diffuseColor = vec3(1.0);
    color *= diffuseColor;
    
    outValue = vec4(color, 1);
    float flogz = 1.0 + (worldViewProjMat * vec4(hitp, 1.0)).w;
    gl_FragDepth = log2(flogz) * Fcoef_half;
}
