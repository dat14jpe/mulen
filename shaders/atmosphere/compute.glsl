
uint udot(uvec3 a, uvec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

uint GetWorkGroupIndex()
{
    return udot(gl_WorkGroupID, uvec3(1, gl_NumWorkGroups.x, gl_NumWorkGroups.x * gl_NumWorkGroups.y));
}

uint GetGlobalIndex()
{
    const uint groupSize = udot(gl_WorkGroupSize, uvec3(1, 1, 1));
    return udot(gl_GlobalInvocationID, uvec3(1, groupSize, groupSize * groupSize));
}
