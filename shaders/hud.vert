#version 450

// 2-D HUD vertex shader
// Each quad is described by two triangles with NDC positions baked in.
// We pass a per-quad color as a vertex attribute.

layout(location = 0) in vec2  inPos;   // NDC [-1, 1]
layout(location = 1) in vec4  inColor; // RGBA

layout(location = 0) out vec4 fragColor;

void main()
{
    gl_Position = vec4(inPos, 0.0, 1.0);
    fragColor   = inColor;
}
