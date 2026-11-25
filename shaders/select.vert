#version 330 core
// Select pass, vertex stage: one quad over the output window.

layout(location = 0) in ivec2 pixel; // quad corner in output pixels

uniform uint screen_width;
uniform uint screen_height;

out vec2 uv; // texture coordinates in the layer array, v = 0 at the top

void main()
{
    float u = float(pixel.x);
    float v = float(pixel.y);
    uv = vec2(u / float(screen_width), v / float(screen_height));
    gl_Position = vec4(2 * u / float(screen_width) - 1, 1 - 2 * v / float(screen_height), 0, 1);
}
