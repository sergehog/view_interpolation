#version 330 core
// Select pass, fragment stage: winner-takes-all over the depth layers.
// Costs are aggregated by averaging mip levels 1-3 of the layer array (2x2, 4x4 and 8x8 box filters).

in vec2 uv;

uniform sampler2DArray Texture; // rgb = mean colour, a = cost (see sweep.frag)
uniform uint layers;

out vec3 color;

float aggregated_cost(uint layer)
{
    vec3 p = vec3(uv, float(layer));
    return (texture(Texture, p, 1).a + texture(Texture, p, 2).a + texture(Texture, p, 3).a)/3;
}

void main()
{
    color = texture(Texture, vec3(uv, 0)).rgb;
    float min_cost = aggregated_cost(0u);
    for (uint layer = 1u; layer < layers; layer++)
    {
        float cost = aggregated_cost(layer);
        if (cost < min_cost)
        {
            min_cost = cost;
            color = texture(Texture, vec3(uv, float(layer))).rgb;
        }
    }
}
