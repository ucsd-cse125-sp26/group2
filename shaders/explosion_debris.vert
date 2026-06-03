/// @file explosion_debris.vert
/// @brief Velocity-stretched explosion debris and ember vertex shader.
#version 450

layout(set = 0, binding = 0) readonly buffer DebrisData { float data[]; };

layout(set = 1, binding = 0) uniform ParticleUniforms {
    mat4  view;
    mat4  proj;
    vec3  camPos;   float _p0;
    vec3  camRight; float _p1;
    vec3  camUp;    float _p2;
} u;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vAge;

const vec2 corners[4] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

void main()
{
    const int stride = 16;
    const int base = gl_InstanceIndex * stride;

    vec3 pos = vec3(data[base + 0], data[base + 1], data[base + 2]);
    float size = data[base + 3];
    vec3 vel = vec3(data[base + 4], data[base + 5], data[base + 6]);
    float stretch = data[base + 7];
    vec4 color = vec4(data[base + 8], data[base + 9], data[base + 10], data[base + 11]);
    vec4 sim = vec4(data[base + 12], data[base + 13], data[base + 14], data[base + 15]);

    vec2 c = corners[gl_VertexIndex % 4];
    float speed = length(vel);
    vec3 along = speed > 1.0 ? vel / speed : u.camUp;
    vec3 side = normalize(cross(along, normalize(u.camPos - pos)));
    vec3 wpos = pos + along * c.x * size * stretch + side * c.y * size;

    gl_Position = u.proj * u.view * vec4(wpos, 1.0);
    vUV = c;
    vColor = color;
    vAge = clamp(sim.x / max(sim.y, 0.001), 0.0, 1.0);
}
