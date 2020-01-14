#version 450

uniform uint numPoints;
uniform float aspect;

out vec3 color;

void main()
{
    float t = float(gl_VertexID) / float(numPoints);
    vec2 scale = vec2(0.5);
    scale.x /= aspect;
    vec3 colors[3] = vec3[](vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1));
    uint num = colors.length();
    uint c0 = uint(t * num); 
    uint c1 = (c0 + 1u) % num;
    color = mix(colors[c0], colors[c1], fract(t * num));
    t *= 2.0 * 3.14159;
    gl_PointSize = 32;
    gl_Position = vec4(scale * vec2(cos(t), sin(t)), 0, 1);
}
