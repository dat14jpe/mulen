#version 450
#include "common.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
#include "compute.glsl"

uniform layout(binding=0, rgba16f) writeonly image3D scatterImage;

void RMuMuSNuFromScatteringFragCoord
(
    vec3 fragCoord, out float r, out float mu, out float mu_s, out float nu,
    out bool intersectsGround
)
{
    const vec4 ScatterSize = vec4(ScatterNuSize - 1, ScatterMuSSize, ScatterMuSize, ScatterRSize);
    float fragCoordNu = floor(fragCoord.x / float(ScatterMuSSize));
    float fragCoordMuS = mod(fragCoord.x, float(ScatterMuSSize));
    vec4 uvwz = vec4(fragCoordNu, fragCoordMuS, fragCoord.y, fragCoord.z) / ScatterSize;
    RMuMuSNuFromScatteringUvwz(uvwz, r, mu, mu_s, nu, intersectsGround);
    nu = clamp(nu, mu * mu_s - sqrt((1.0 - mu*mu) * (1.0 - mu_s*mu_s)),
        mu * mu_s + sqrt((1.0 - mu*mu) * (1.0 - mu_s*mu_s)));
}

void ComputeSingleScatteringTexture(vec3 fragCoord, out vec3 rayleigh, out vec3 mie)
{
    float r, mu, mu_s, nu;
    bool intersectsGround;
    RMuMuSNuFromScatteringFragCoord(fragCoord, r, mu, mu_s, nu, intersectsGround);
    ComputeSingleScattering(r, mu, mu_s, nu, intersectsGround, rayleigh, mie);
}

void main()
{
    vec3 coords = vec3(gl_GlobalInvocationID.xyz) + vec3(0.5);
    vec3 rayleigh, mie;
    ComputeSingleScatteringTexture(coords, rayleigh, mie);
    imageStore(scatterImage, ivec3(gl_GlobalInvocationID), vec4(rayleigh, mie.r));
}
