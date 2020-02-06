#version 450

#include "../noise.glsl"
#include "../geometry.glsl"
#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, rgba16f) writeonly image3D lightImage;

float PlanetShadow(vec3 ori, vec3 dir, vec3 planetCenter, float voxelSize)
{
    // - to do: maybe decrease planet radius by approximately one voxel length, to avoid overshadowing?
    float R = planetRadius;// - voxelSize; // - possibly remove the subtraction if gradual shadowing is implemented
    
    vec3 offs = planetCenter - ori;
    float d = dot(offs, dir);
    if (d < 0.0) 
    {
        //return 1.0; // wrong? Possibly
        return (length(ori - planetCenter) - R) / voxelSize + 1.0;
    }
    offs -= dir * dot(offs, dir);
    // - this should also depend on distance and angular size of the sun, no? To do
    float s = (length(offs) - R) / voxelSize + 1.0;
    return s;
}

vec3 perpendicular(vec3 v)
{
    vec3 a = cross(vec3(1, 0, 0), v), b = cross(vec3(0, 1, 0), v);
    return dot(a, a) > 1e-5 ? a : b;
}

vec3 offsetOrigin(vec3 p, vec3 dir, float voxelSize)
{
    const float PI = 3.141592653589793;
    
    // - the ray *origin* has to be offset in a plane with normal dir, right? Hmm...
    vec3 a = perpendicular(dir);
    vec3 b = cross(dir, a);
    const float offs = 1.0; // - to do
    return vec3(0.0);
    float r = rand3D(p) * 0.5 + 0.5, theta = 2 * PI * (rand3D(p.xyz) * 0.5 + 05);
    float x = sqrt(r) * cos(theta);
    float y = sqrt(r) * sin(theta);
    return 
        //dir * voxelSize 
        //p * voxelSize
        + offs * voxelSize * (x * a + y * b)
        ;
}

void main()
{
    const uint loadId = GetWorkGroupIndex();
    const UploadBrick upload = uploadBricks[loadId];
    uvec3 writeOffs = BrickIndexTo3D(upload.brickIndex) * BrickRes + gl_LocalInvocationID;
    
    vec3 lp = vec3(gl_LocalInvocationID) / float(BrickRes - 1u) * 2 - 1;
    vec3 gp = (upload.nodeLocation.xyz + upload.nodeLocation.w * lp);
    vec3 p = gp * atmosphereScale;
    
    
    const float voxelSize = 1.0 / float(BrickRes - 1u) * 2 * upload.nodeLocation.w * atmosphereScale * planetRadius;
    const float stepFactor = 0.1 * stepSize; // - to-be-tuned
    const float atmScale = atmosphereRadius;
    float dist = 0.0;
    const vec3 dir = lightDir;
    const vec3 ori = p * planetRadius + offsetOrigin(p, dir, voxelSize);
    
    vec3 light = vec3(1.0);
    {
        //dist += voxelSize * 1.0 * (rand(gp.xy, gp.z)); // - to do: make this work
        
        float shadow = PlanetShadow(ori, dir, vec3(0.0), voxelSize);
        //shadow = min(1.0, shadow); // - test (probably bad, for interpolation. Or not?)
        //shadow = 1.0; // - debugging banding
        light = vec3(1.0); // - debugging
        
        float opticalDepthR = 0.0, opticalDepthM = 0.0;
        float prevDensityR = 0.0, prevDensityM = 0.0; // - maybe to do: get these from start values
        
        //if (false)
        if (shadow > 0.0
            && length(ori - planetLocation) < planetRadius + atmosphereHeight) // only ray trace for voxels within the atmosphere
        {
            light = vec3(1.0);
            {
                // - shouldn't need higher factor than 1 or 2, no?
                dist += 1 * stepFactor * upload.nodeLocation.w * atmScale;
            }
            
            
            float tmin, tmax;
            float R = planetRadius + atmosphereHeight;
            if (IntersectSphere(ori, dir, planetLocation, R, tmin, tmax)) // atmosphere intersection
            {
                const uint maxSteps = 512u; // - arbitrary, for testing
                float maxDist = tmax;
                
                if (IntersectSphere(ori, dir, planetLocation, planetRadius - voxelSize, tmin, tmax))
                {
                    maxDist = min(maxDist, tmin);
                }
             
                uint depth; // - to do: initialise correctly
                vec3 nodeCenter = upload.nodeLocation.xyz;
                float nodeSize = upload.nodeLocation.w;
                
                uint numBricks = 0u, numSteps = 0u;
                uint ni = upload.nodeIndex;
                while (InvalidIndex != ni)
                {
                    nodeCenter *= atmScale;
                    nodeSize *= atmScale;
                    const float step = nodeSize / atmScale * stepFactor;
                    const float atmStep = step * atmScale;
                    
                    AabbIntersection(tmin, tmax, vec3(-nodeSize) + nodeCenter, vec3(nodeSize) + nodeCenter, ori, dir);
                    tmax = min(tmax, maxDist);
                    
                    const vec3 brickOffs = vec3(BrickIndexTo3D(ni));
                    vec3 localStart = (ori - nodeCenter) / nodeSize;
                    vec3 lc = localStart + dist / nodeSize * dir;
                    
                    //do // do while to not erroneously miss the first voxel if it's on the border
                    while (dist < tmax && numSteps < maxSteps)
                    {
                        vec3 tc = lc * 0.5 + 0.5;
                        tc = clamp(tc, vec3(0.0), vec3(1.0));
                        tc = BrickSampleCoordinates(brickOffs, tc);
                        vec4 voxelData = texture(brickTexture, tc);
                        
                        float rayleigh = voxelData.x, mie = voxelData.y;
                        float densityR = RayleighDensityFromSample(rayleigh);
                        float densityM = MieDensityFromSample(mie);
                        
                        const bool midPoint = false;//true;
                        if (midPoint)
                        {
                            opticalDepthR += densityR * atmStep;
                            opticalDepthM += densityM * atmStep;
                        }
                        else
                        {
                            opticalDepthR += (prevDensityR + densityR) * 0.5 * atmStep;
                            opticalDepthM += (prevDensityM + densityM) * 0.5 * atmStep;
                        }
                        
                        prevDensityR = densityR;
                        prevDensityM = densityM;
                        
                        dist += atmStep;
                        lc = localStart + dist / nodeSize * dir;
                        ++numSteps;
                    } //while (dist < tmax && numSteps < maxSteps);
                    ++numBricks;
                    
                    const uint old = ni;
                    vec3 p = (ori + dist * dir) / atmScale;
                    //if (length(p * atmScale) < planetRadius * 0.99) break; // - testing (but this should be done accurately and once with a maxDist update)
                    if (dist > maxDist) break;
                    ni = OctreeDescendMap(p, nodeCenter, nodeSize, depth);
                    if (old == ni) break; // - error (but can this even happen?)
                    
                    if (numBricks >= 64u) break; // - testing
                    // - maybe also try breaking if transmittance is too high (though needlessly evaluating it might be expensive in itself)
                }
                //if (numBricks > 10u) shadow = 4.0; // - testing
                //if (numBricks == 1) light = vec3(0, 1, 1) * 1e2; // - debugging
            }
        }
        
        vec3 transmittance = exp(-(opticalDepthR * betaR + opticalDepthM * betaMEx));
        light *= transmittance;
        light *= shadow;
    }
    
    //light = max(light, vec3(0.0));
    imageStore(lightImage, ivec3(writeOffs), vec4(light, 0.0));
}
