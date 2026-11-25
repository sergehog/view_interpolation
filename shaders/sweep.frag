#version 330 core
// Sweep pass, fragment stage: photo-consistency of one depth hypothesis.
// Output rgb = mean colour of the two inputs, a = matching cost in [0, 1].

in vec3 uv1_g; // homogeneous texture coordinates, see sweep.vert
in vec3 uv2_g;

uniform sampler2D Texture1; // inputs hold BGR, hence the .bgr swizzles
uniform sampler2D Texture2;
uniform float param1;       // cost threshold from the config (<param1>), 0 = default

const float default_cost_threshold = 21.0 / 255;

out vec4 color_cost;

bool inside(vec3 uv_h, vec2 uv)
{
    return uv_h.z > 0 && all(greaterThanEqual(uv, vec2(0))) && all(lessThanEqual(uv, vec2(1)));
}

void main()
{
    vec2 uv1 = uv1_g.xy / uv1_g.z;
    vec2 uv2 = uv2_g.xy / uv2_g.z;
    vec3 color1 = textureLod(Texture1, uv1, 0).bgr;
    vec3 color2 = textureLod(Texture2, uv2, 0).bgr;
    vec3 coarse1 = textureLod(Texture1, uv1, 2).bgr; // 4x4 neighbourhood
    vec3 coarse2 = textureLod(Texture2, uv2, 2).bgr;

    vec3 diff = abs(color1 - color2) / 4 + abs(coarse1 - coarse2) / 4;
    float cost = diff.r + diff.g + diff.b;
    float threshold = param1 > 0 ? param1 : default_cost_threshold;
    cost = cost > threshold ? 1.0 : cost / threshold;

    // A point behind an input camera or outside its image cannot be matched
    // (clamp-to-edge sampling would otherwise produce false, often perfect, matches).
    bool valid1 = inside(uv1_g, uv1);
    bool valid2 = inside(uv2_g, uv2);
    if (!(valid1 && valid2))
    {
        cost = 1.0;
        color1 = valid1 ? color1 : color2;
        color2 = valid2 ? color2 : color1;
    }
    color_cost = vec4((color1 + color2) / 2, cost);
}
