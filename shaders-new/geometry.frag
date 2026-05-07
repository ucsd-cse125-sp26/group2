#version 450

layout(location = 0) in vec3 frag_normal;
layout(location = 1) in vec2 frag_vt;

layout(location = 0) out vec4 color;

layout(set = 2, binding = 0) uniform sampler2D tex;

// Just a single directional light for now...
const vec3 light_direction = normalize(-vec3(1.0f,1.0f,1.0f));
const vec4 light_color = vec4(1.0f,1.0f,1.0f,1.0f);
const vec4 ambient_color = vec4(normalize(vec3(0.08f, 0.08f,0.12f)),1.0f); // dark-blue

void main()
{
    vec4 albedo = texture(tex, frag_vt);
    float cosT = max(0.0f,dot(-light_direction,frag_normal));
    vec4 irradiance = light_color * cosT + ambient_color;
    //color = albedo * irradiance;
    color = albedo;
    //color = vec4(1.0f);
}
