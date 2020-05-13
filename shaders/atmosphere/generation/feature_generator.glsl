#version 450
#include "../generation.glsl"

float ComputeMieDensity(DensityComputationParams params)
{
    vec3 p = params.p;
    float h = params.h;
    float mie = 0.0;
    
    // - to do: loop over features (in genData)
    
    return mie;
}
