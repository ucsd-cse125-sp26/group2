/// @file ribbon.vert
/// @brief Ribbon trail vertex shader using pre-expanded vertex buffer.
#version 450

/// @brief Vertex buffer: pre-expanded RibbonVertex (32 bytes).
///   location 0: vec4 (pos.xyz + pad)
///   location 1: vec4 color (pre-multiplied alpha)
layout(location = 0) in vec4 inPosP;   // .xyz = world pos, .w = padding
layout(location = 1) in vec4 inColor;

/// @brief Per-frame camera uniforms.
layout(set = 1, binding = 0) uniform ParticleUniforms {
    mat4  view;
    mat4  proj;
    vec3  camPos;   float _p0;
    vec3  camRight; float _p1;
    vec3  camUp;    float _p2;
} u;

layout(location = 0) out vec4 vColor;

void main()
{
    gl_Position = u.proj * u.view * vec4(inPosP.xyz, 1.0);
    vColor = inColor;
}
