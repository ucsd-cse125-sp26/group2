#version 450

layout(location = 0) in vec3 v;     // Model vertex position
layout(location = 1) in vec3 vn;    // Model normal
layout(location = 2) in vec2 vt;    // Model texture coord
layout(location = 3) in vec4 tangent; // Model tangent, w = bitangent sign

layout(location = 0) out vec3 frag_normal;
layout(location = 1) out vec2 frag_vt;
layout(location = 2) out vec4 frag_tangent;

layout(set = 1, binding = 0) uniform Camera {
    mat4 view_projection;
} camera;

layout(set = 1, binding = 1) uniform Object {
    mat4 model;
} object;

void main()
{
    mat3 normalMatrix = transpose(inverse(mat3(object.model)));
    gl_Position = camera.view_projection * object.model * vec4(v, 1.0f);
    frag_normal = normalize(normalMatrix * vn);
    frag_vt = vt;
    frag_tangent = vec4(normalize(mat3(object.model) * tangent.xyz), tangent.w);
}
