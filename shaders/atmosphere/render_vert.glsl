#version 450

out vec4 ndc;

void main()
{
    const vec2 coords[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1), vec2(0, 1));
    ndc = vec4(coords[gl_VertexID] * 2 - 1, 1, 1);
    gl_Position = ndc;
}

