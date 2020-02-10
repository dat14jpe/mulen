#version 450

#include "common.glsl"
#include "../geometry.glsl"
#include "../noise.glsl"

layout(location = 0) out vec4 outValue;
in vec4 ndc;
//in float flogz;

bool RenderPlanet(vec3 ori, vec3 dir)
{
    const vec3 center = planetLocation;
    float t0, t1;
    if (!IntersectSphere(ori, dir, center, planetRadius, t0, t1)) return false;
    if (t0 <= 0.0) return false; // don't block if we're inside the planet (since it's fun to look out into the atmosphere shell)
    
    // - to do: dither (somehow proportionally to voxel size, of course)
    const vec2 randTimeOffs = vec2(cos(time), sin(time));
    float randOffs = rand(gl_FragCoord.xy + randTimeOffs);
    randOffs *= 0.0 / 512.0 * planetRadius; // - probably not right. To do: tune
    // - no, offsetting the distance doesn't seem to be a good way... so can we "blur" along the surface?
    
    
    vec3 hitp = ori + t0 * dir;
    vec3 normal = normalize(hitp - center);
    
    vec3 color = vec3(max(0.0, dot(normal, lightDir))); // Lambertian
    //color = vec3(1.0); // - testing only atmosphere lighting
    vec3 diffuseColor = vec3(1.0);
    diffuseColor = vec3(0.01, 0.05, 0.1);
    diffuseColor = pow(vec3(0.016, 0.306, 0.482), vec3(2.2));
    diffuseColor = vec3(0.0); // - testing (though this seems more correct than having a color)
    color *= diffuseColor;
    //color = vec3(0.0);
    
    { // sunglint (specular)
        vec3 d = reflect(lightDir, normal);
        float s = pow(max(0.0, dot(d, dir)), 80.0);
        color += s * vec3(1.0);
    }
    
    //if (false) // - debugging
    { // - testing modulation by atmosphere lighting
        // - the sample position probably needs to be dithered in time... no?
        vec3 p = (hitp + dir * randOffs - center) / atmosphereScale / planetRadius;
        vec3 nodeCenter;
        float nodeSize;
        uint depth;
        uint ni = OctreeDescendMap(p, nodeCenter, nodeSize, depth);
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
    //color *= 10.0; // - to do: uniform
    
    outValue = vec4(color, 1);
    //float flogz = 1.0 + (worldViewProjMat * vec4(hitp, 1.0)).w;
    float flogz = 1.0 + (viewProjMat * vec4(hitp, 1.0)).w;
    gl_FragDepth = log2(flogz) * Fcoef_half;
    return true;
}

bool RenderSun(vec3 ori, vec3 dir)
{
    const vec3 center = lightDir * sun.x;
    float t0, t1;
    if (!IntersectSphere(ori, dir, center, sun.y, t0, t1)) return false;
    // - to do: simple bloom effect around it? A dedicated bloom pass would be better, but maybe
    vec3 color = vec3(1.0) * 1.0;//sun.z;
    outValue = vec4(color, 1);
    gl_FragDepth = 0.5;
    // - to do
    return true;
}

void main()
{
    const vec3 ori = vec3(invViewMat * vec4(0, 0, 0, 1));
    const vec3 dir = normalize(vec3(invViewProjMat * ndc));
    
    if (!RenderPlanet(ori, dir) && !RenderSun(ori, dir)) discard;
}
