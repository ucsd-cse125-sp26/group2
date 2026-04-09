#version 450

layout(location = 0) in  vec2  vUV;
layout(location = 1) in  float vOpacity;

layout(set = 2, binding = 0) uniform sampler2D decalAtlas;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 tex = texture(decalAtlas, vUV);
    outColor = tex * vOpacity;
}
