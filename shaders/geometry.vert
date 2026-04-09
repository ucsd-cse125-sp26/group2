#version 450

layout(location = 0) out vec3 diffuse;
layout(location = 1) flat out vec3 fragNormal;

layout(set = 1, binding = 0) uniform Matrices
{
    mat4 model;
    mat4 view;
    mat4 projection;
} ubo;

const float width = 1.0f;
const float width_over_2 = width * 0.5f;

const vec3 cubeMin = -vec3(width_over_2,width_over_2,width_over_2);
const vec3 cubeMax = -cubeMin;
    const vec3 positions[24] = vec3[](
        // Front
        vec3(cubeMin.x, cubeMin.y, cubeMax.z),
        vec3(cubeMax.x, cubeMin.y, cubeMax.z),
        vec3(cubeMax.x, cubeMax.y, cubeMax.z),
        vec3(cubeMin.x, cubeMax.y, cubeMax.z),

        // Back
        vec3(cubeMax.x, cubeMin.y, cubeMin.z),
        vec3(cubeMin.x, cubeMin.y, cubeMin.z),
        vec3(cubeMin.x, cubeMax.y, cubeMin.z),
        vec3(cubeMax.x, cubeMax.y, cubeMin.z),

        // Top
        vec3(cubeMin.x, cubeMax.y, cubeMax.z),
        vec3(cubeMax.x, cubeMax.y, cubeMax.z),
        vec3(cubeMax.x, cubeMax.y, cubeMin.z),
        vec3(cubeMin.x, cubeMax.y, cubeMin.z),

        // Bottom
        vec3(cubeMin.x, cubeMin.y, cubeMin.z),
        vec3(cubeMax.x, cubeMin.y, cubeMin.z),
        vec3(cubeMax.x, cubeMin.y, cubeMax.z),
        vec3(cubeMin.x, cubeMin.y, cubeMax.z),

        // Left
        vec3(cubeMin.x, cubeMin.y, cubeMin.z),
        vec3(cubeMin.x, cubeMin.y, cubeMax.z),
        vec3(cubeMin.x, cubeMax.y, cubeMax.z),
        vec3(cubeMin.x, cubeMax.y, cubeMin.z),

        // Right
        vec3(cubeMax.x, cubeMin.y, cubeMax.z),
        vec3(cubeMax.x, cubeMin.y, cubeMin.z),
        vec3(cubeMax.x, cubeMax.y, cubeMin.z),
        vec3(cubeMax.x, cubeMax.y, cubeMax.z)
    );

    // Specify normals
    const vec3 normals[24] = vec3[](
        // Front
        vec3(0, 0, 1),
        vec3(0, 0, 1),
        vec3(0, 0, 1),
        vec3(0, 0, 1),

        // Back
        vec3(0, 0, -1),
        vec3(0, 0, -1),
        vec3(0, 0, -1),
        vec3(0, 0, -1),

        // Top
        vec3(0, 1, 0),
        vec3(0, 1, 0),
        vec3(0, 1, 0),
        vec3(0, 1, 0),

        // Bottom
        vec3(0, -1, 0),
        vec3(0, -1, 0),
        vec3(0, -1, 0),
        vec3(0, -1, 0),

        // Left
        vec3(-1, 0, 0),
        vec3(-1, 0, 0),
        vec3(-1, 0, 0),
        vec3(-1, 0, 0),

        // Right
        vec3(1, 0, 0),
        vec3(1, 0, 0),
        vec3(1, 0, 0),
        vec3(1, 0, 0)
    );

    // Specify indices
    const uint indices[36] = uint[](
        0, 1, 2, 0, 2, 3,        // Front
        4, 5, 6, 4, 6, 7,        // Back
        8, 9, 10, 8, 10, 11,     // Top
        12, 13, 14, 12, 14, 15,  // Bottom
        16, 17, 18, 16, 18, 19,  // Left
        20, 21, 22, 20, 22, 23   // Right
    );

const vec3 colors[6] = vec3[](
    vec3(1,0,0), vec3(0,1,0), vec3(0,0,1),
    vec3(1,1,0), vec3(1,0,1), vec3(0,1,1)
);

void main()
{
    vec4 p = vec4(positions[indices[gl_VertexIndex]], 1.0f);
    mat4 mvp = ubo.projection * ubo.view * ubo.model;
    gl_Position = mvp * p;
    diffuse = colors[gl_VertexIndex / 6];
    fragNormal = vec3(ubo.model * vec4(normals[indices[gl_VertexIndex]],0.0f));
}