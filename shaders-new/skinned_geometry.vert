#version 450

// Skinned vertex shader for animated characters AND the first-person weapon
// viewmodel. Outputs match geometry.vert (normal, uv, worldPos, tangent) so the
// textured skinned pipeline can share geometry_shadowed.frag (point lights +
// shadows). The untextured player pipeline (debug.frag) and the chams pipeline
// read only normal/uv and ignore the extra outputs.

struct SkinnedInstance
{
    mat4 worldTransform;         ///< Rig-local → world.  Composed CPU-side as T * R * S.
    uint paletteBase;               ///< First joint slot in the palette = instanceIndex * numJoints.
    uint materialId;                ///< Reserved; ignore for now (will index a material table later).
    uint _pad0;
    uint _pad1;
    vec4 tint; ///< rgb = per-player color, a = blend factor (0 = no tint).
};

layout(location = 0) in vec3 v;     // Model vertex position
layout(location = 1) in vec3 vn;    // Model normal
layout(location = 2) in vec2 vt;    // Model texture coord
layout(location = 3) in vec4 tangent;    // Model tangent, w = bitangent sign

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
                             boneWeights.w * pallete[skinedInst.paletteBase + boneIndices.w] ;

    // Full skinned world transform, so worldPos + normal + tangent are correct
    // for the animated pose (point-light + shadow lighting needs the real
    // world-space position and a pose-deformed normal).
    mat4 model = skinedInst.worldTransform * skinningTransform;
    vec4 worldPos = model * vec4(v, 1.0f);

    gl_Position = camera.view_projection * worldPos;

    mat3 normalMatrix = transpose(inverse(mat3(model)));
    frag_normal = normalize(normalMatrix * vn);
    frag_tangent = vec4(normalize(mat3(model) * tangent.xyz), tangent.w);
    frag_worldPos = worldPos.xyz;
    frag_vt = vt;
}
