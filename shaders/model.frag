/// @file model.frag
/// @brief Fragment shader for Assimp-loaded mesh geometry.
///
/// Samples the per-mesh base-colour texture and applies two-light diffuse + ambient.
///
/// SDL3 GPU SPIR-V resource layout for fragment shaders:
///   set = 2 -> sampled textures / storage textures / storage buffers  <- texDiffuse here
///   set = 3 -> uniform buffers
#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec2 fragTexCoord;

/// @brief Base-colour texture (or 1x1 white fallback).
layout(set = 2, binding = 0) uniform sampler2D texDiffuse;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 n = normalize(fragNormal);

    // Primary light — top-right-front
    vec3  dir0  = normalize(vec3( 0.5,  1.0,  0.5));
    float diff0 = max(dot(n, dir0), 0.0);

    // Secondary fill light — left-back, weaker
    vec3  dir1  = normalize(vec3(-0.5,  0.3, -0.8));
    float diff1 = max(dot(n, dir1), 0.0) * 0.35;

    float ambient  = 0.35;
    float lighting = ambient + diff0 * 0.55 + diff1;

    vec4 texColor = texture(texDiffuse, fragTexCoord);
    outColor      = vec4(texColor.rgb * lighting, texColor.a);
}
