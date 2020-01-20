#version 450

out vec4 ndc;
out float flogz;
uniform float Fcoef;

void main()
{
    const vec2 coords[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1), vec2(0, 1));
    ndc = vec4(coords[gl_VertexID] * 2 - 1, 1, 1);
    gl_Position = ndc;
    
    // https://outerra.blogspot.com/2013/07/logarithmic-depth-buffer-optimizations.html
    gl_Position.z = log2(max(1e-6, 1.0 + gl_Position.w)) * Fcoef - 1.0;
    flogz = 1.0 + gl_Position.w;
}

