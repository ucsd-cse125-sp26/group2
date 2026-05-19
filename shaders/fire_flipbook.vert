/// @file fire_flipbook.vert
/// @brief Camera-facing preview billboard for the generated fire flipbook atlas.
#version 450

layout(set = 1, binding = 0) uniform FlipbookVertexUniforms {
    mat4 view;
    mat4 proj;
    vec3 camRight; float halfWidth;
    vec3 camUp;    float halfHeight;
    vec3 center;   float opacity;
} u;

layout(location = 0) out vec2 vUV;
layout(location = 1) out float vOpacity;

const vec2 corners[6] = vec2[](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
    vec2( 1.0,  1.0), vec2(-1.0,  1.0), vec2(-1.0, -1.0)
);

void main()
{
    vec2 c = corners[gl_VertexIndex];
    vec3 world = u.center + u.camRight * c.x * u.halfWidth + u.camUp * c.y * u.halfHeight;
    gl_Position = u.proj * u.view * vec4(world, 1.0);
    vUV = c * 0.5 + 0.5;
    vOpacity = u.opacity;
}
