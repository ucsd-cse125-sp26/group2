#version 450

// SmokeParticle (64 bytes = 16 floats):
//   [0..2]  pos.xyz  [3] size
//   [4..7]  color (pre-multiplied rgba)
//   [8]     rotation  [9] normalizedAge  [10] maxLifetime  [11] _pad

layout(set = 0, binding = 0) readonly buffer SmokeData { float data[]; };

layout(set = 1, binding = 0) uniform ParticleUniforms {
    mat4  view;
    mat4  proj;
    vec3  camPos;   float _p0;
    vec3  camRight; float _p1;
    vec3  camUp;    float _p2;
} u;

layout(location = 0) out vec2  vUV;
layout(location = 1) out vec4  vColor;
layout(location = 2) out float vAge;

const vec2 corners[4] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

void main()
{
    const int stride = 12; // floats (SmokeParticle = 48 bytes)
    const int base   = gl_InstanceIndex * stride;

    const vec3  pos      = vec3(data[base+0], data[base+1], data[base+2]);
    const float size     = data[base+3];
    const vec4  color    = vec4(data[base+4], data[base+5],
                                data[base+6], data[base+7]);
    const float rotation = data[base+8];
    const float age      = data[base+9];

    const vec2 c = corners[gl_VertexIndex % 4];

    // Apply rotation around camera-forward axis
    float cosR  = cos(rotation);
    float sinR  = sin(rotation);
    vec2  rotUV = vec2(c.x * cosR - c.y * sinR,
                       c.x * sinR + c.y * cosR);

    const vec3 wpos = pos
        + u.camRight * rotUV.x * size
        + u.camUp    * rotUV.y * size;

    gl_Position = u.proj * u.view * vec4(wpos, 1.0);
    vUV   = c;
    vColor = color;
    vAge   = age;
}
