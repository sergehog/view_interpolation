#version 330 core
// Debug variant of shaders/select.frag: outputs the winning depth layer as grey (black = maxZ, white = minZ).
// Use it with <fragment_shader>tools/select_depth.frag</fragment_shader>, or DEPTH=1 tools/eval.sh.

in vec2 uv;

uniform sampler2DArray Texture;
uniform uint layers;

out vec3 color;

float aggregated_cost(uint layer)
{
    vec3 p = vec3(uv, float(layer));
    return (texture(Texture, p, 1).a + texture(Texture, p, 2).a + texture(Texture, p, 3).a) / 3;
}

void main()
{
    color = vec3(0);
    float min_cost = aggregated_cost(0u);
    for (uint layer = 1u; layer < layers; layer++)
    {
        float cost = aggregated_cost(layer);
        if (cost < min_cost)
        {
            min_cost = cost;
            color = vec3(float(layer) / float(layers));
        }
    }
}
