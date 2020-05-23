#version 450

layout(location = 0) out vec4 outValue;
in vec4 ndc;
uniform layout(location = 0) sampler2D postTexture;

void main()
{
    vec3 color = vec3(texture(postTexture, ndc.xy * 0.5 + 0.5));
    outValue = vec4(color, 1.0);
}
