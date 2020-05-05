
#include "../noise.glsl"
#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, rg8) writeonly image3D brickImage;
uniform uint brickUploadOffset;

float fBm(uint octaves, vec3 p, float persistence, float lacunarity)
{
    float a = 1.0;
    float v = 0.0;
    for (uint i = 0u; i < octaves; ++i)
    {
        v += a * (noise(p) * 2.0 - 1.0);
        a *= persistence;
        p *= lacunarity;
    }
    return v;
}
