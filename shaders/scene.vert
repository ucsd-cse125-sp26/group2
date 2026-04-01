#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

// SDL3 GPU Vulkan layout: sets[0]=vertex resources, sets[1]=vertex uniforms,
// sets[2]=fragment resources, sets[3]=fragment uniforms.
// Vertex uniform buffer slot 0 → set=1, binding=0.
layout(set = 1, binding = 0) uniform Matrices {
    mat4 viewProj;
    mat4 model;
} u;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = u.viewProj * u.model * vec4(inPosition, 1.0);
    fragColor   = inColor;
}
