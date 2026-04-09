// model.vert — vertex shader for Assimp-loaded mesh geometry.
//
// Vertex inputs: position (location 0), normal (location 1), texCoord (location 2).
// Uniform buffer: same Matrices struct as projective.vert (model / view / projection).
//
// SDL3 GPU SPIR-V resource layout for vertex shaders:
//   set = 0 → sampled textures / storage textures / storage buffers
//   set = 1 → uniform buffers  ← Matrices lives here
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragWorldPos;

layout(set = 1, binding = 0) uniform Matrices
{
    mat4 model;
    mat4 view;
    mat4 projection;
} ubo;

void main()
{
    vec4 worldPos  = ubo.model * vec4(inPosition, 1.0);
    gl_Position    = ubo.projection * ubo.view * worldPos;

    // Transform normal into world space using the normal matrix
    // (inverse-transpose of the model matrix).
    fragNormal   = normalize(mat3(transpose(inverse(ubo.model))) * inNormal);
    fragWorldPos = worldPos.xyz;
}
