#version 450

#include "../noise.glsl"
#include "common.glsl"
#include "lighting.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, r8) writeonly image3D lightImage;
uniform uint brickUploadOffset;


void main()
{
    const uint loadId = GetWorkGroupIndex() + brickUploadOffset;
    const UploadBrick upload = uploadBricks[loadId];
    uvec3 writeOffs = BrickIndexTo3D(upload.brickIndex) * BrickRes + gl_LocalInvocationID;
    
    const uint nodeIndex = upload.nodeIndex;
    const uint depth = DepthFromInfo(nodeGroups[nodeIndex / NodeArity].info);
    
    vec3 lp = vec3(gl_LocalInvocationID) / float(BrickRes - 1u) * 2 - 1;
    vec3 gp = (upload.nodeLocation.xyz + upload.nodeLocation.w * lp);
    vec3 p = gp * atmosphereScale;
    
    
    const float voxelSize = 1.0 / float(BrickRes - 1u) * 2 * upload.nodeLocation.w * atmosphereScale * planetRadius;
    const float atmScale = atmosphereRadius;
    float dist = 0.0;
    
    if (false)
    {
        // - experiment: clamp p to spherical position
        vec3 pp = p * planetRadius;
        float len = length(pp);
        float shell = ceil(len / voxelSize);
        pp = pp / len * shell * voxelSize;
        p = pp / planetRadius;
    }
    if (false)
    {
        if (length(p) < 1.01) p = normalize(p);
    }
    
    const vec3 dir = lightDir.xyz;
    vec3 ori = p * planetRadius;
    
    const float stepFactor = 
        //0.1
        0.4 // - probably too high, no? Use lower depth instead (to do)
        * stepSize; // - to-be-tuned
    
    vec3 light = vec3(1.0);
    {
        float dist = 0.0;
        //dist += voxelSize * 1.0 * (rand(gp.xy, gp.z)); // - to do: make this work
        
        float shadow = PlanetShadow(ori, dir, vec3(0.0), voxelSize);
        //shadow = min(1.0, shadow); // - test (probably bad, for interpolation. Or not?)
        //shadow = 1.0; // - debugging banding
        light = vec3(1.0);
        //light *= 0.0; // - testing // - even with this there are light "grooves" near the surface; this means interior voxels are the cause. Fix it
        
        //ori += offsetOrigin(p, dir, voxelSize); // - trying this *after* the planet shadowing. But it's not helping.
        
        //if (false)
        if (shadow > 0.0
            && length(ori) < planetRadius + atmosphereHeight // only ray trace for voxels within the atmosphere
            )
        {
            //dist += 1 * stepFactor * upload.nodeLocation.w * atmScale; // avoid self-shadowing
            dist += sqrt(2.0) * voxelSize; // avoid self-shadowing
            //ori += 0.5 * voxelSize * normalize(p); // trying to avoid more self-shadowing, but... incorrect?
            
            OctreeTraversalData o;
            o.center = upload.nodeLocation.xyz;
            o.size = upload.nodeLocation.w;
            o.ni = upload.nodeIndex;
            light *= TraceTransmittance(ori, dir, dist, o, stepFactor, depth);
            //light *= ConeTraceTransmittance(ori, dir, dist, stepFactor, voxelSize);
            
            
            if (false)
            { // - experimenting with supersampling:
                vec3 a = perpendicular(dir);
                vec3 b = cross(dir, a);
                
                /*// - testing tangent plane instead: (to no discernible improvement)
                vec3 n = normalize(p);
                a = perpendicular(n);
                b = cross(n, a);*/
                
                float num = 0.0;
                light = vec3(0.0);
                int ires = 4; // - tested up to 4, with no noticeable improvements over 2 (which were already minimal)
                float res = float(ires);
                for (int iy = 0; iy < ires; ++iy)
                for (int ix = 0; ix < ires; ++ix)
                {
                    float x = float(ix) / (res - 1) * 2 - 1, y = float(iy) / (res - 1) * 2 - 1;
                    vec3 p = ori + (a * x + b * y) * voxelSize * 0.5;
                    light += TraceTransmittance(p, dir, dist, o, stepFactor, depth);
                    num += 1.0;
                }
                light /= num;
                
                /*
                // - just testing:
                light += TraceTransmittance(ori + dir * voxelSize, dir, dist, nodeCenter, nodeSize, ni, stepFactor);
                light *= 0.5;*/
            }
        }
        // - outdated by newer transmittance method, no? But its absence causes more banding now
        //light *= shadow;
        //if (shadow < 0.0) light = vec3(shadow); // - test
    }
    
    imageStore(lightImage, ivec3(writeOffs), vec4(light, 0.0));
}
