/// @file volume_fire.vert
/// @brief Proxy-box vertex shader for animated raymarched fire volumes.
#version 450

layout(set = 0, binding = 0) readonly buffer VolumeFireInstances { float data[]; };

layout(set = 1, binding = 0) uniform ParticleUniforms {
    mat4  view;
    mat4  proj;
    vec3  camPos;   float _p0;
    vec3  camRight; float _p1;
    vec3  camUp;    float _p2;
} u;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) flat out vec3 vBoxMin;
layout(location = 2) flat out vec3 vBoxMax;
layout(location = 3) flat out float vOpacity;
layout(location = 4) flat out vec3 vCamPos;

const vec3 cubeVerts[36] = vec3[](
    vec3(0,0,0), vec3(1,1,0), vec3(1,0,0), vec3(0,0,0), vec3(0,1,0), vec3(1,1,0),
    vec3(1,0,1), vec3(0,1,1), vec3(0,0,1), vec3(1,0,1), vec3(1,1,1), vec3(0,1,1),
    vec3(0,0,1), vec3(0,1,0), vec3(0,0,0), vec3(0,0,1), vec3(0,1,1), vec3(0,1,0),
    vec3(1,0,0), vec3(1,1,1), vec3(1,0,1), vec3(1,0,0), vec3(1,1,0), vec3(1,1,1),
    vec3(0,1,0), vec3(0,1,1), vec3(1,1,1), vec3(0,1,0), vec3(1,1,1), vec3(1,1,0),
    vec3(0,0,1), vec3(1,0,0), vec3(1,0,1), vec3(0,0,1), vec3(0,0,0), vec3(1,0,0)
);

void main()
{
    const int stride = 8;
    const int base = gl_InstanceIndex * stride;

    vBoxMin = vec3(data[base + 0], data[base + 1], data[base + 2]);
    float opacity = data[base + 3];
    vBoxMax = vec3(data[base + 4], data[base + 5], data[base + 6]);
    vWorldPos = mix(vBoxMin, vBoxMax, cubeVerts[gl_VertexIndex]);
    vOpacity = opacity;
    vCamPos = u.camPos;

    gl_Position = u.proj * u.view * vec4(vWorldPos, 1.0);
}
