/// @file pbr.vert
/// @brief PBR vertex shader for Assimp-loaded meshes.
#version 450

/// @brief Vertex attributes (must match ModelVertex layout: 48 bytes).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;   // xyz = tangent, w = bitangent sign (±1)

/// @brief Outputs to fragment shader.
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;

/// @brief Per-frame camera and model matrices.
layout(set = 1, binding = 0) uniform Matrices
{
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 normalMatrix;   // transpose(inverse(model)), padded to mat4 for std140
} ubo;

void main()
{
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    fragWorldPos  = worldPos.xyz;
    gl_Position   = ubo.projection * ubo.view * worldPos;

    // Transform normal and tangent to world space via the normal matrix.
    mat3 nMat     = mat3(ubo.normalMatrix);
    fragNormal    = normalize(nMat * inNormal);
    fragTangent   = normalize(nMat * inTangent.xyz);
    fragBitangent = cross(fragNormal, fragTangent) * inTangent.w;

    fragTexCoord  = inTexCoord;
}
