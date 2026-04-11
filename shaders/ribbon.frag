/// @file ribbon.frag
/// @brief Ribbon trail fragment shader with pre-multiplied alpha passthrough.
#version 450

layout(location = 0) in  vec4 vColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vColor; // pre-multiplied alpha baked in by CPU
}
