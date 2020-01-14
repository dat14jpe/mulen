#version 450

layout(location = 0) out vec4 outValue;
uniform vec2 pixelSize;

in vec3 color;

void main()
{
    float v = distance(gl_PointCoord, vec2(0.5));
    const float w = 0.2;
    const float first = 0.2, second = 0.4;
    v = smoothstep(first, first + w, v) * (1.0 - smoothstep(second, second + w, v));
    outValue = vec4(color, v);
}
