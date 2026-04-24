#version 450

layout(location = 0) in vec3 frag_normal;
layout(location = 1) in vec2 frag_vt;

layout(location = 0) out vec4 color;

layout(set = 2, binding = 0) uniform sampler2D tex;

void main()
{
    color = texture(tex, frag_vt);
}
