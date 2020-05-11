#version 450

#include "lighting.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, r8) writeonly image3D lightImage;
uniform uint brickUploadOffset;
uniform layout(binding=3) sampler2D groupLightMap;


void main()
{
    const uint loadId = GetWorkGroupIndex() + brickUploadOffset;
    const UploadBrick upload = uploadBricks[loadId];
    uvec3 writeOffs = BrickIndexTo3D(upload.brickIndex) * BrickRes + gl_LocalInvocationID;
    
    const uint nodeIndex = upload.nodeIndex;
    const uint depth = DepthFromInfo(nodeGroups[nodeIndex / NodeArity].info);
    const float nodeSize = upload.nodeLocation.w;
    
    vec3 lp = vec3(gl_LocalInvocationID) / float(BrickRes - 1u) * 2 - 1;
    vec3 gp = upload.nodeLocation.xyz + nodeSize * lp;
    vec3 p = gp * atmosphereScale;
    
    vec3 groupPos = 0.5 * (lp + (vec3(nodeIndex & 1u, (nodeIndex >> 1u) & 1u, (nodeIndex >> 2u) & 1u) * 2.0 - 1.0));
    
    
    const float voxelSize = 1.0 / float(BrickRes - 1u) * 2 * upload.nodeLocation.w * atmosphereScale * planetRadius;
    const float atmScale = atmosphereRadius;
    float dist = 0.0;
    
    
    const vec3 dir = lightDir.xyz;
    vec3 ori = p * planetRadius;
    
    const float stepFactor = 
        //0.1
        0.4 // - probably too high, no? Use lower depth instead (to do)
        * stepSize; // - to-be-tuned
    
    vec3 light = vec3(1.0);
    float maxDist = 1e30;
    
    if (usePerGroupLighting)
    {
        // Begin with sampling from the per-group shadowing
        vec3 samplePos = vec3(invGroupLightMat * vec4(groupPos, 1.0));
        // - to do: check correctness
        vec2 sampleCoords = samplePos.xy * 0.5 + 0.5; // - is this correct? To test...
        uvec2 uTexBase = GroupIndexTo3D(upload.brickIndex / NodeArity).xy * GroupRes;
        sampleCoords = (vec2(uTexBase) + vec2(0.5) + float(GroupRes - 1u) * sampleCoords) / textureSize(groupLightMap, 0);
        float groupShadow = texture(groupLightMap, sampleCoords).r;
        light *= groupShadow;
        // - to do: determine max distance to trace to (into per-group shadow map)
        maxDist = (sqrt(3.0) - dot(dir, groupPos)) // - to do: check this
            //4.0 // - hardcoded approximation
            * 2.0 * nodeSize * atmosphereScale * planetRadius;
    }
    
    if (light != vec3(0.0))
    // - trace to per-group shadowing:
    // (to do: correct start offset)
    {
        float dist = 0.0;
        //dist += voxelSize * 1.0 * (rand(gp.xy, gp.z)); // - to do: make this work
        
        float shadow = PlanetShadow(ori, dir, vec3(0.0), voxelSize);
        //light = vec3(1.0);
        
        //ori += offsetOrigin(p, dir, voxelSize); // - trying this *after* the planet shadowing. But it's not helping.
        
        //if (false)
        if (shadow > 0.0
            && length(ori) < planetRadius + atmosphereHeight // only ray trace for voxels within the atmosphere
            )
        {
            dist += sqrt(2.0) * voxelSize; // avoid self-shadowing
            
            light *= TraceTransmittance(ori, dir, dist, stepFactor, depth, maxDist);
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
                    light += TraceTransmittance(p, dir, dist, stepFactor, depth);
                    num += 1.0;
                }
                light /= num;
            }
        }
    }
    
    imageStore(lightImage, ivec3(writeOffs), vec4(light, 0.0));
}
