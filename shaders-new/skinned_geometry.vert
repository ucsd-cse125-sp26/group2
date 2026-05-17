#version 450

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
layout(location = 3) in vec4 tangent;    // Model texture coord

layout(location = 4) in ivec4 boneIndices;     // Model vertex position
layout(location = 5) in vec4 boneWeights;    // Model normal

layout(location = 0) out vec3 frag_normal;     // Model vertex position
layout(location = 1) out vec2 frag_vt;    // Model normal

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

    vec4 skinnedVertex = skinningTransform * vec4(v, 1.0f) ;
    gl_Position = camera.view_projection * skinedInst.worldTransform * skinnedVertex;

    mat3 vectorWorldTransform = transpose(inverse(mat3(skinedInst.worldTransform)));
    frag_normal = normalize(vectorWorldTransform* vn);
    frag_vt = vt;
}
