#version 330 core
// Sweep pass, vertex stage: one quad over the virtual image per depth layer.
// For the layer's depth hypothesis, back-projects the quad corners and projects them into both inputs.

layout(location = 0) in ivec2 pixel; // quad corner in virtual-image pixels, [0, width] x [0, height]

uniform uint width;  // processing resolution
uniform uint height;
uniform uint layer;  // current depth layer, 0 = maxZ ... layers-1 = minZ
uniform uint layers;
uniform mat4 MVP1;   // (u*z, v*z, z, 1) of the virtual camera -> (x*w, y*w, w, 1) in input 1
uniform mat4 MVP2;
uniform float minNZ; // 1/minZ - 1/maxZ
uniform float maxNZ; // 1/maxZ

// Homogeneous texture coordinates in the inputs; the fragment shader divides .xy by .z.
// For a fixed depth they are linear in (u, v), so the rasterizer interpolates them exactly.
// (Dividing here would be exact only at the corners: wrong for non-parallel cameras.)
out vec3 uv1_h;
out vec3 uv2_h;

vec3 to_texture(vec4 p) // pixel-centre coordinates -> [0, 1] texture coordinates, still homogeneous
{
    return vec3((p.x + 0.5 * p.z) / float(width), (p.y + 0.5 * p.z) / float(height), p.z);
}

void main()
{
    float u = float(pixel.x);
    float v = float(pixel.y);
    float d = float(layer) / float(max(layers, 2u) - 1u);
    float z = 1.0 / (d * minNZ + maxNZ); // depth hypotheses are uniform in inverse depth

    // (u, v) are raster coordinates (pixel x spans [x, x+1]); cameras use pixel centres
    vec4 point = vec4((u - 0.5) * z, (v - 0.5) * z, z, 1);
    uv1_h = to_texture(MVP1 * point);
    uv2_h = to_texture(MVP2 * point);
    gl_Position = vec4(2 * u / float(width) - 1, 2 * v / float(height) - 1, 0, 1);
}
