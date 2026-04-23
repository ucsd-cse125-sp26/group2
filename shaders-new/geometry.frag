// normal.frag
#version 450

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;
layout(location = 0) out vec4 color;

// Just a single directional light for now...
const vec3 light_direction = normalize(-vec3(1.0f,1.0f,1.0f));
const vec3 light_color = vec3(1.0f,1.0f,1.0f);
const vec3 ambient_color = vec3(0.08f, 0.08f,0.12f); // dark-blue

void main()
{
    float cosT = max(0.0f,dot(-light_direction,frag_normal));
    vec3 irradiance = light_color * cosT + ambient_color;
    // color = vec4(frag_color * irradiance,1.0f);
    color = vec4(frag_color ,1.0f);
}
