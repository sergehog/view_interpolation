#version 330 core
// Sweep pass, geometry stage: routes the quad of the current depth layer to its texture-array slice.

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

in vec3 uv1_h[];
in vec3 uv2_h[];

out vec3 uv1_g;
out vec3 uv2_g;

uniform uint layer;

void main()
{
    for (int i = 0; i < gl_in.length(); ++i)
    {
        gl_Layer = int(layer);
        gl_Position = gl_in[i].gl_Position;
        uv1_g = uv1_h[i];
        uv2_g = uv2_h[i];
        EmitVertex();
    }
    EndPrimitive();
}
