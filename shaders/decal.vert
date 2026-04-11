#version 450

// DecalInstance (64 bytes = 16 floats):
//   [0..2]  pos.xyz   [3] size
//   [4..6]  right.xyz [7] _p0
//   [8..10] up.xyz    [11] opacity
//   [12..13] uvMin    [14..15] uvMax

layout(set = 0, binding = 0) readonly buffer DecalData { float data[]; };

layout(set = 1, binding = 0) uniform ParticleUniforms {
    mat4  view;
    mat4  proj;
    vec3  camPos;   float _p0;
    vec3  camRight; float _p1;
    vec3  camUp;    float _p2;
} u;

layout(location = 0) out vec2  vUV;
layout(location = 1) out float vOpacity;

const vec2 corners[4] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

void main()
{
    const int stride = 16;
    const int base   = gl_InstanceIndex * stride;

    const vec3  pos     = vec3(data[base+0], data[base+1], data[base+2]);
    const float size    = data[base+3];
    const vec3  right   = vec3(data[base+4], data[base+5], data[base+6]);
    const vec3  up      = vec3(data[base+8], data[base+9], data[base+10]);
    const float opacity = data[base+11];
    const vec2  uvMin   = vec2(data[base+12], data[base+13]);
    const vec2  uvMax   = vec2(data[base+14], data[base+15]);

    const vec2 c    = corners[gl_VertexIndex % 4];
    const vec3 wpos = pos + right * c.x * size + up * c.y * size;

    gl_Position = u.proj * u.view * vec4(wpos, 1.0);

    // Map corner [-1,1] to UV range
    vUV      = mix(uvMin, uvMax, c * 0.5 + 0.5);
    vOpacity = opacity;
}
