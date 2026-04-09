#version 450

// Storage buffer: one BillboardParticle per instance
// Matches struct BillboardParticle in ParticleTypes.hpp
layout(set = 0, binding = 0) readonly buffer Particles {
    // pos(3) + size(1) + color(4) + vel(3) + lifetime(1) = 12 floats = 48 bytes
    vec4 posSize[];     // .xyz = pos, .w = size  (offset 0)
    // NOTE: GLSL storage buffers must use a single array; we index manually.
};

// Raw struct access via a flat float buffer
layout(set = 0, binding = 0) readonly buffer ParticleData {
    float data[];
};

layout(set = 1, binding = 0) uniform ParticleUniforms {
    mat4  view;
    mat4  proj;
    vec3  camPos;  float _p0;
    vec3  camRight; float _p1;
    vec3  camUp;    float _p2;
} u;

layout(location = 0) out vec2  vUV;
layout(location = 1) out vec4  vColor;

// Quad corners: (-1,-1), (+1,-1), (+1,+1), (-1,+1)
const vec2 corners[4] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

void main()
{
    // Each particle is 12 floats (48 bytes)
    const int stride = 12;
    const int base   = gl_InstanceIndex * stride;

    const vec3 pos   = vec3(data[base + 0], data[base + 1], data[base + 2]);
    const float size = data[base + 3];
    const vec4 color = vec4(data[base + 4], data[base + 5],
                            data[base + 6], data[base + 7]);
    // vel at base+8..10, lifetime at base+11 — not needed in vertex shader

    const vec2 c    = corners[gl_VertexIndex % 4];
    const vec3 wpos = pos + u.camRight * c.x * size + u.camUp * c.y * size;

    gl_Position = u.proj * u.view * vec4(wpos, 1.0);
    vUV         = c;
    vColor      = color;
}
