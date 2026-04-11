/// @file lightning_arc.vert
/// @brief Lightning arc vertex shader using pre-expanded ArcVertex buffer.
#version 450

/// @brief Vertex buffer: ArcVertex (32 bytes).
///   location 0: vec4 (pos.xyz + edge)
///   location 1: vec4 color
layout(location = 0) in vec4 inPosEdge;  // .xyz = world pos, .w = edge (-1..+1)
layout(location = 1) in vec4 inColor;

/// @brief Per-frame camera uniforms.
layout(set = 1, binding = 0) uniform ParticleUniforms {
    mat4  view;
    mat4  proj;
    vec3  camPos;   float _p0;
    vec3  camRight; float _p1;
    vec3  camUp;    float _p2;
} u;

layout(location = 0) out float vEdge;
layout(location = 1) out vec4  vColor;

void main()
{
    gl_Position = u.proj * u.view * vec4(inPosEdge.xyz, 1.0);
    vEdge  = inPosEdge.w;
    vColor = inColor;
}
