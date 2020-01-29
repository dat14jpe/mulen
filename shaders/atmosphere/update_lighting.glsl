#version 450

#include "../noise.glsl"
#include "../geometry.glsl"
#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, rgba16f) writeonly image3D brickImage;

float PlanetShadow(vec3 ori, vec3 dir, vec3 planetCenter, float voxelSize)
{
    // - to do: maybe decrease planet radius by approximately one voxel length, to avoid overshadowing?
    float R = planetRadius;// - voxelSize; // - possibly remove the subtraction if gradual shadowing is implemented
    
    vec3 offs = planetCenter - ori;
    float d = dot(offs, dir);
    if (d < 0.0) return 1.0;
    offs -= dir * dot(offs, dir);
    // - this should also depend on distance and angular size of the sun, no? To do
    float s = (length(offs) - R) / voxelSize + 1.0;
    return s;
    return clamp(s, 0.0, 1.0);
    
    float t0, t1;
    // - to do: make planet-shadowing gradual instead of absolute
    return IntersectSphere(ori, dir, planetCenter, R, t0, t1) ? 0.0 : 1.0;
}

void main()
{
    const uint loadId = GetWorkGroupIndex();
    const UploadBrick upload = uploadBricks[loadId];
    uvec3 writeOffs = BrickIndexTo3D(upload.brickIndex) * BrickRes + gl_LocalInvocationID;
    
    vec3 lp = vec3(gl_LocalInvocationID) / float(BrickRes - 1u) * 2 - 1;
    vec3 gp = (upload.nodeLocation.xyz + upload.nodeLocation.w * lp);
    vec3 p = gp * atmosphereScale;
    
    
    const float stepFactor = 0.2 * stepSize; // - to-be-tuned
    const float atmScale = atmosphereRadius;
    float dist = 0.0;
    
    vec3 light = vec3(0.0);
    {
        const float voxelSize = 1.0 / float(BrickRes - 1u) * 2 * upload.nodeLocation.w * atmosphereScale * planetRadius;
        const vec3 ori = p * planetRadius;
        const vec3 dir = lightDir;
        
        float shadow = PlanetShadow(ori, dir, vec3(0.0), voxelSize);
        //if (shadow > 0.0)
        {
            light = vec3(1.0);
            {
                // - should need higher factor than 1 or 2, no?
                dist += 1 * stepFactor * upload.nodeLocation.w * atmScale;
            }
            
            float tmin, tmax;
            float R = planetRadius + atmosphereHeight;
            if (IntersectSphere(ori, dir, planetLocation, R, tmin, tmax)) // atmosphere intersection
            {
                float depthR = 0.0;
                
                const uint maxSteps = 512u; // - arbitrary, for testing
                const float maxDist = tmax;
                // - to do: ray march the octree, from current position to tmax distance
             
                float alpha = 0.0;   
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
                        if (alpha > 0.999) break; // - to do: tune
                        
                        vec3 tc = lc * 0.5 + 0.5;
                        tc = clamp(tc, vec3(0.0), vec3(1.0));
                        tc = BrickSampleCoordinates(brickOffs, tc);
                        vec4 voxelData = texture(brickTexture, tc);
                        
                        float density = voxelData.x;
                        // - to do: separate Rayleigh and Mie
                        depthR += atmStep * exp(voxelData.x);
                        //shadow -= density * step * 1e2; // - testing
                        light *= vec3(1.0 - density);
                        
                        dist += atmStep;
                        lc = localStart + dist / nodeSize * dir;
                        ++numSteps;
                    } //while (dist < tmax && numSteps < maxSteps);
                    ++numBricks;
                    
                    const uint old = ni;
                    vec3 p = (ori + dist * dir) / atmScale;
                    if (dist > maxDist) break;
                    ni = OctreeDescend(p, nodeCenter, nodeSize, depth);
                    
                    if (numBricks >= 64u) break; // - testing
                }
                //if (numBricks == 1) light = vec3(0, 1, 1) * 1e2; // - debugging
                
                vec3 transmittance = betaR * exp(-depthR);
                // - to do: scatter towards camera, then store
            }
            light *= shadow;
            //light = vec3(shadow);
        }
    }
    
    //light = max(light, vec3(0.0));
    imageStore(brickImage, ivec3(writeOffs), vec4(light, 0.0));
}
