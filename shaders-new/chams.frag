#version 450

// Killcam "chams" silhouette: flat red. Paired with the skinned geometry vertex
// shader (same interface as debug.frag) but bound to a pipeline with a GREATER
// depth test, so it only rasterises where the killer is occluded by world
// geometry — a wallhack-style red silhouette.

layout(location = 0) in vec3 frag_normal;
layout(location = 1) in vec2 frag_vt;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 normalColor;

layout(set = 3, binding = 0) uniform ChamsTint {
    vec4 tint;
} chams;

void main()
{
    color = chams.tint;
    normalColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
}
