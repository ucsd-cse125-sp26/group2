#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

// SDL3 GPU Vulkan layout: fragment uniforms → set=3, binding=0
layout(set = 3, binding = 0) uniform FragTint
{
    vec4 tint; // xyz = RGB multiplier, w = alpha
} u;

void main()
{
    outColor = vec4(fragColor, 1.0) * u.tint;
}
