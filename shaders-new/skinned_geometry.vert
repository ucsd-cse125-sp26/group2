#version 450

// Skinned vertex shader for animated characters AND the first-person weapon
// viewmodel.  Outputs match geometry.vert (normal, uv, worldPos, tangent) so
// the skinned pipeline can share geometry_shadowed.frag (point lights + shadows).

struct SkinnedInstance
{
    mat4 worldTransform;
    uint paletteBase;
    uint materialId;
    uint _pad0;
    uint _pad1;
    vec4 tint;
};

layout(location = 0) in vec3 v;          // position
layout(location = 1) in vec3 vn;         // normal
layout(location = 2) in vec2 vt;         // uv
layout(location = 3) in vec4 tangent;    // tangent (w = bitangent sign)
layout(location = 4) in ivec4 boneIndices;
layout(location = 5) in vec4 boneWeights;

layout(location = 0) out vec3 frag_normal;
layout(location = 1) out vec2 frag_vt;
layout(location = 2) out vec3 frag_worldPos;
layout(location = 3) out vec4 frag_tangent;

layout(set = 0, binding = 0) readonly buffer BonePallete {
    mat4 pallete[];
};

layout(set = 0, binding = 1) readonly buffer Instances {
    SkinnedInstance instances[];
};

layout(set = 1, binding = 0) uniform Camera {
    mat4 view_projection;
} camera;

void main()
{
    SkinnedInstance skinedInst = instances[gl_InstanceIndex];

    mat4 skinningTransform = boneWeights.x * pallete[skinedInst.paletteBase + boneIndices.x] +
                             boneWeights.y * pallete[skinedInst.paletteBase + boneIndices.y] +
                             boneWeights.z * pallete[skinedInst.paletteBase + boneIndices.z] +
                             boneWeights.w * pallete[skinedInst.paletteBase + boneIndices.w];

    // Full skinned world transform (so worldPos + normal are correct for the
    // animated pose, which point-light + shadow lighting needs).
    mat4 model = skinedInst.worldTransform * skinningTransform;
    vec4 worldPos = model * vec4(v, 1.0f);

    gl_Position = camera.view_projection * worldPos;

    mat3 normalMat = transpose(inverse(mat3(model)));
    frag_normal = normalize(normalMat * vn);
    frag_tangent = vec4(normalize(mat3(model) * tangent.xyz), tangent.w);
    frag_worldPos = worldPos.xyz;
    frag_vt = vt;
}
