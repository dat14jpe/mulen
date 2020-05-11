#version 450

#include "lighting.glsl"
layout(local_size_x = GroupRes, local_size_y = GroupRes, local_size_z = 1) in;
#include "compute.glsl"

uniform layout(binding=0, r16) writeonly image2D lightImage;
uniform uint brickUploadOffset;

void main()
{
    if (!usePerGroupLighting) return;
    
    const uint loadId = GetWorkGroupIndex() * NodeArity + brickUploadOffset;
    const UploadBrick upload = uploadBricks[loadId];
    uvec3 writeOffs = GroupIndexTo3D(upload.brickIndex / NodeArity) * GroupRes + gl_LocalInvocationID;
    
    const uint nodeIndex = upload.nodeIndex;
    const uint depth = DepthFromInfo(nodeGroups[nodeIndex / NodeArity].info);
    
    
    const vec3 dir = lightDir.xyz;
    
    vec3 groupPos = vec3(gl_LocalInvocationID) / float(GroupRes - 1u) * 2 - 1;
    //groupPos.z = 1.0; // - seemingly (visually) "fixes" the problem. So the offset might be wrong?
    groupPos = vec3(groupLightMat * vec4(groupPos, 1.0));
    vec3 gp = upload.nodeLocation.xyz + upload.nodeLocation.w * (groupPos * 2.0 + 1.0);
    vec3 p = gp * atmosphereScale;
    
    
    const float voxelSize = 1.0 / float(BrickRes - 1u) * 2 * upload.nodeLocation.w * atmosphereScale * planetRadius;
    const float atmScale = atmosphereRadius;
    float dist = 0.0;
    
    
    vec3 ori = p * planetRadius;
    
    const float stepFactor = 
        //0.1
        0.4 // - probably too high, no? Use lower depth instead (to do)
        * stepSize; // - to-be-tuned
    
    vec3 light = vec3(1.0);
    {
        float dist = 0.0;
        
        float shadow = PlanetShadow(ori, dir, vec3(0.0), voxelSize);
        light = vec3(1.0);
        
        //if (false)
        if (shadow > 0.0
            && length(ori) < planetRadius + atmosphereHeight // only ray trace for voxels within the atmosphere
            )
        {
            dist += sqrt(2.0) * voxelSize; // avoid self-shadowing
            light *= TraceTransmittance(ori, dir, dist, stepFactor, depth);
        }
    }
    
    imageStore(lightImage, ivec2(writeOffs.xy), vec4(light, 0.0));
}
