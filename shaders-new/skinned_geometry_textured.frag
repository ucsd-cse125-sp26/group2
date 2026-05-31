#version 450

// Textured fragment shader for the animated first-person weapon viewmodel.
// Samples the weapon/arms diffuse (col) texture and applies a single directional
// light + ambient — no normal-debug tint.

layout(location = 0) in vec3 frag_normal;
layout(location = 1) in vec2 frag_vt;

layout(location = 0) out vec4 color;

layout(set = 2, binding = 0) uniform sampler2D tex;

const vec3 light_direction = normalize(-vec3(1.0, 1.0, 1.0));
const vec3 ambient_color = vec3(0.32, 0.32, 0.35);

void main()
{
    // The skinned (CharacterRig) load path does NOT flip V (unlike the static
    // loader's flipUVs), so flip it here to match the authored textures.
    vec4 albedo = texture(tex, vec2(frag_vt.x, 1.0 - frag_vt.y));

    // Two-sided lighting: the viewmodel uses a negative-X scale, so winding /
    // normal sign can't be trusted — use abs() so both faces are lit.
    vec3 n = normalize(frag_normal);
    float cosT = abs(dot(-light_direction, n));
    vec3 lit = albedo.rgb * (cosT + ambient_color);
    color = vec4(lit, albedo.a);
}
