#version 450

layout(location = 0) in vec3 frag_normal;
layout(location = 1) in vec2 frag_vt;

layout(location = 0) out vec4 color;


// Just a single directional light for now...
const vec3 light_direction = normalize(-vec3(1.0f,1.0f,1.0f));
const vec4 light_color = vec4(1.0f,1.0f,1.0f,1.0f);
const vec4 ambient_color = vec4(normalize(vec3(0.08f, 0.08f,0.12f)),1.0f); // dark-blue

void main()
{
    vec3 normal = normalize(gl_FrontFacing ? frag_normal : -frag_normal);

    // Flat surface albedo (no normal-direction debug tint) so the character
    // shows its actual shading instead of rainbow-by-normal colours.
    vec4 albedo = vec4(1.0f);
    float cosT = max(0.0f, dot(-light_direction, normal));
    vec4 irradiance = light_color * cosT + ambient_color;

    color = albedo * irradiance;
}
