#version 450

layout(location = 0) out vec4 outValue;
in vec4 ndc;
uniform mat4 invWorldViewMat, invWorldViewProjMat;
uniform sampler2D lightTexture;

void main()
{
    vec3 color = vec3(texture(lightTexture, ndc.xy * 0.5 + 0.5));
    // - to do: tone mapping
    color = pow(color, vec3(1.0 / 2.2)); // gamma correction
    // - to do: dither
    outValue = vec4(color, 1.0);
}
