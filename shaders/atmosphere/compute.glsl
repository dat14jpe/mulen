
uint udot(uvec3 a, uvec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

uint GetWorkGroupIndex()
{
    return udot(gl_WorkGroupID, uvec3(1, gl_NumWorkGroups.x, gl_NumWorkGroups.x * gl_NumWorkGroups.y));
}

uint GetGlobalIndex()
{
    const uint groupSize = gl_WorkGroupSize.x * gl_WorkGroupSize.y * gl_WorkGroupSize.z;
    return udot(gl_GlobalInvocationID, uvec3(1, groupSize * gl_NumWorkGroups.x, groupSize * gl_NumWorkGroups.x * gl_NumWorkGroups.y));
}
