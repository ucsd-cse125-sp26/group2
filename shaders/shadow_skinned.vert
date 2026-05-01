/// @file shadow_skinned.vert
/// @brief Depth-only vertex shader with GPU skinning + instancing for shadows.
#version 450

layout(location = 0) in vec3  inPosition;
layout(location = 4) in ivec4 inBoneIndices;
layout(location = 5) in vec4  inBoneWeights;

layout(std430, set = 0, binding = 0) readonly buffer BonePalette
{
    mat4 palette[];
} bones;

struct InstanceData
{
    mat4 worldTransform;
    uint paletteBase;
    uint materialId;
    uint _pad0;
    uint _pad1;
};

layout(std430, set = 0, binding = 1) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

/// Reuses the existing LightMatrices UBO layout (lightVP + model).  The
/// per-frame caller pushes lightVP for the current cascade; `model` is
/// ignored on the skinned path.
layout(set = 1, binding = 0) uniform LightMatrices
{
    mat4 lightVP;
    mat4 model; // unused for skinned path
};

void main()
{
    InstanceData inst = instances[gl_InstanceIndex];

    mat4 skin = bones.palette[inst.paletteBase + uint(inBoneIndices.x)] * inBoneWeights.x
              + bones.palette[inst.paletteBase + uint(inBoneIndices.y)] * inBoneWeights.y
              + bones.palette[inst.paletteBase + uint(inBoneIndices.z)] * inBoneWeights.z
              + bones.palette[inst.paletteBase + uint(inBoneIndices.w)] * inBoneWeights.w;

    vec4 skinned  = skin * vec4(inPosition, 1.0);
    vec4 worldPos = inst.worldTransform * skinned;
    gl_Position   = lightVP * worldPos;
}
