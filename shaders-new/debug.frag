#version 450

layout(location = 0) in vec3 frag_normal;
layout(location = 1) in vec2 frag_vt;
layout(location = 4) in vec4 frag_tint;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normalColor;


// Just a single directional light for now...
const vec3 light_direction = normalize(-vec3(1.0f,1.0f,1.0f));
const vec4 light_color = vec4(1.0f,1.0f,1.0f,1.0f);
const vec4 ambient_color = vec4(normalize(vec3(0.08f, 0.08f,0.12f)),1.0f); // dark-blue

void main()
{
    vec3 normal = normalize(gl_FrontFacing ? frag_normal : -frag_normal);

    // Player instances pass their assigned color in frag_tint. Keep lighting
    // intact, but replace the model albedo when the tint blend is 1.
    vec4 albedo = vec4(mix(vec3(1.0f), frag_tint.rgb, frag_tint.a), 1.0f);
    float cosT = max(0.0f, dot(-light_direction, normal));
    vec4 irradiance = light_color * cosT + ambient_color;

    color = albedo * irradiance;
    normalColor = vec4(normal * 0.5 + 0.5, 1.0);
}
