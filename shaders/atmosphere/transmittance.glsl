#version 450
#include "common.glsl"

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;
#include "compute.glsl"

uniform layout(binding=0, rgba16f) writeonly image2D transmittanceImage;

vec3 ComputeTransmittance(float r0, float mu)
{
    const int NumSteps = 512; // (nothing special with 512, apart from powers of two just being nice numbers)
    float dx = DistanceToAtmosphereTop(r0, mu) / float(NumSteps);
    float lastDensityR = 0.0, lastDensityM = 0.0;
    float opticalDepthR = 0.0, opticalDepthM = 0.0;
    for (int step = 0; step <= NumSteps; ++step)
    {
        float t = dx * float(step);
        float r = sqrt(t*t + r0*r0 + 2.0*t*r0*mu);
        float h = r - Rg;
        
        float densityR = exp(-h / HR);
        float densityM = exp(-h / HM);
        opticalDepthR += (densityR + lastDensityR) * 0.5 * dx;
        opticalDepthM += (densityM + lastDensityM) * 0.5 * dx;
        lastDensityR = densityR;
        lastDensityM = densityM;
        // (maybe add ozone and homogeneous Mie here too (at least ozone))
    }
    vec3 opticalDepth = betaR * opticalDepthR + vec3(betaMEx * opticalDepthM);
    return exp(-opticalDepth);
}

void main()
{
    vec2 coords = (vec2(gl_GlobalInvocationID.xy) + vec2(0.5)) / vec2(imageSize(transmittanceImage));
    float r, mu;
    TransmittanceUvToRMu(coords, r, mu);
    vec3 transmittance = ComputeTransmittance(r, mu);
    imageStore(transmittanceImage, ivec2(gl_GlobalInvocationID), vec4(transmittance, 0.0));
}
