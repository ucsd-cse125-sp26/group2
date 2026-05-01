/// @file pbr_skinned.vert
/// @brief PBR vertex shader with GPU linear-blend skinning + hardware instancing.
///
/// Replaces the per-entity CPU-skinning + vertex re-upload path.  Each draw
/// call is `drawIndexedInstanced(numChars)` against the shared rig VB/IB.
/// The shader reads:
///   - per-vertex bind-pose data (location 0..3, identical to pbr.vert)
///   - per-vertex bone indices + weights (location 4..5, second vertex buffer)
///   - per-frame bone palette SSBO   (set=0, binding=0): mat4 * (numChars * numJoints)
///   - per-frame instance data SSBO  (set=0, binding=1): worldTransform + paletteBase
///   - per-frame view/proj UBO       (set=1, binding=0): same `Matrices` block as pbr.vert
///
/// Note: matches `loadShaderFromFile(... samplerCount=0, uniformBufferCount=1,
/// storageBufferCount=2 ...)` on the renderer side.
#version 450

layout(location = 0) in vec3  inPosition;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inTexCoord;
layout(location = 3) in vec4  inTangent;
layout(location = 4) in ivec4 inBoneIndices;
layout(location = 5) in vec4  inBoneWeights;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;

/// std140 layout: aligned mat4 inside the SSBO.
layout(std430, set = 0, binding = 0) readonly buffer BonePalette
{
    mat4 palette[];
} bones;

/// One per character per frame.  `paletteBase` is the index of this
/// instance's first joint in `bones.palette`, i.e. instanceIdx * numJoints.
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

layout(set = 1, binding = 0) uniform Matrices
{
    mat4 model;        // unused for the skinned path; kept for layout parity with pbr.vert
    mat4 view;
    mat4 projection;
    mat4 normalMatrix; // unused for the skinned path
} ubo;

void main()
{
    InstanceData inst = instances[gl_InstanceIndex];

    // Linear-blend skinning: weighted average of the four influencing bones.
    mat4 skin = bones.palette[inst.paletteBase + uint(inBoneIndices.x)] * inBoneWeights.x
              + bones.palette[inst.paletteBase + uint(inBoneIndices.y)] * inBoneWeights.y
              + bones.palette[inst.paletteBase + uint(inBoneIndices.z)] * inBoneWeights.z
              + bones.palette[inst.paletteBase + uint(inBoneIndices.w)] * inBoneWeights.w;

    // Pose -> rig-local -> world.
    vec4 skinned     = skin * vec4(inPosition, 1.0);
    vec4 worldPos    = inst.worldTransform * skinned;
    fragWorldPos     = worldPos.xyz;
    gl_Position      = ubo.projection * ubo.view * worldPos;

    // Skin the normal/tangent in rig-local space, then rotate into world space.
    // We assume LBS without non-uniform scale, so mat3(skin) is rigid enough that
    // the inverse-transpose collapses to the rotation portion.
    mat3 skinRot     = mat3(skin);
    mat3 worldRot    = mat3(inst.worldTransform);
    vec3 nLocal      = skinRot * inNormal;
    vec3 tLocal      = skinRot * inTangent.xyz;

    fragNormal       = normalize(worldRot * nLocal);
    fragTangent      = normalize(worldRot * tLocal);
    fragBitangent    = cross(fragNormal, fragTangent) * inTangent.w;
    fragTexCoord     = inTexCoord;
}
