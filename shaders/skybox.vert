// skybox.vert — cube geometry for environment skybox.
// Draw with 36 vertices, no vertex buffer.  Depth is set to max (w)
// so the skybox only fills pixels where no geometry was drawn.
#version 450

layout(location = 0) out vec3 fragDir;

layout(set = 1, binding = 0) uniform SkyboxMatrices
{
    mat4 viewRotation;   // view matrix with translation zeroed (rotation only)
    mat4 projection;
};

// Unit cube: 36 vertices (6 faces × 2 triangles × 3 verts).
const vec3 positions[36] = vec3[](
    // +X face
    vec3( 1, -1, -1), vec3( 1, -1,  1), vec3( 1,  1,  1),
    vec3( 1, -1, -1), vec3( 1,  1,  1), vec3( 1,  1, -1),
    // -X face
    vec3(-1, -1,  1), vec3(-1, -1, -1), vec3(-1,  1, -1),
    vec3(-1, -1,  1), vec3(-1,  1, -1), vec3(-1,  1,  1),
    // +Y face
    vec3(-1,  1,  1), vec3(-1,  1, -1), vec3( 1,  1, -1),
    vec3(-1,  1,  1), vec3( 1,  1, -1), vec3( 1,  1,  1),
    // -Y face
    vec3(-1, -1, -1), vec3(-1, -1,  1), vec3( 1, -1,  1),
    vec3(-1, -1, -1), vec3( 1, -1,  1), vec3( 1, -1, -1),
    // +Z face
    vec3(-1, -1,  1), vec3( 1, -1,  1), vec3( 1,  1,  1),
    vec3(-1, -1,  1), vec3( 1,  1,  1), vec3(-1,  1,  1),
    // -Z face
    vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
    vec3( 1, -1, -1), vec3(-1,  1, -1), vec3( 1,  1, -1)
);

void main()
{
    vec3 pos = positions[gl_VertexIndex];
    fragDir  = pos;   // local-space position IS the sampling direction

    vec4 clipPos = projection * viewRotation * vec4(pos, 1.0);
    // Force depth = 1.0 (far plane) so skybox is always behind geometry.
    gl_Position = clipPos.xyww;
}
