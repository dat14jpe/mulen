#version 450
#include "ACES.glsl"

// - to do: move to common include file
float random(vec2 st) { return fract(sin(dot(st.xy, vec2(12.9898,78.233)))*43758.5453123); }

layout(location = 0) out vec4 outValue;
in vec4 ndc;
uniform layout(location = 0) sampler2D lightTexture;

void main()
{
    vec3 color = vec3(texture(lightTexture, ndc.xy * 0.5 + 0.5));
    
    // Tone mapping:
    color = ACESFitted(color); // - experimental
    
    // Gamma correction:
    color = pow(color, vec3(1.0 / 2.2));
    
    // Dithering (break up colour banding due to limited output bit depth):
    const float invColorDepth = 1.0 / 256.0;
    color += vec3(random(gl_FragCoord.xy)) * invColorDepth;
    
    outValue = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
}
