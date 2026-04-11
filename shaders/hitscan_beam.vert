/// @file hitscan_beam.vert
/// @brief Hitscan beam vertex shader with camera-facing oriented quads.
#version 450

/// @brief HitscanBeam (64 bytes = 16 floats):
///   [0..2]  origin.xyz  [3] radius
///   [4..6]  hitPos.xyz  [7] lifetime
///   [8..11] coreColor   [12..15] edgeColor

/// @brief Per-beam SSBO data.
layout(set = 0, binding = 0) readonly buffer BeamData { float data[]; };

/// @brief Per-frame camera uniforms.
layout(set = 1, binding = 0) uniform ParticleUniforms {
    mat4  view;
    mat4  proj;
    vec3  camPos;   float _p0;
    vec3  camRight; float _p1;
    vec3  camUp;    float _p2;
} u;

layout(location = 0) out vec2  vUV;
layout(location = 1) out float vBrightness;
layout(location = 2) out vec4  vCoreColor;
layout(location = 3) out vec4  vEdgeColor;

void main()
{
    const int stride = 16;
    const int base   = gl_InstanceIndex * stride;

    const vec3  origin    = vec3(data[base+0], data[base+1], data[base+2]);
    const float radius    = data[base+3];
    const vec3  hitPos    = vec3(data[base+4], data[base+5], data[base+6]);
    const float lifetime  = data[base+7];
    const vec4  coreColor = vec4(data[base+ 8], data[base+ 9],
                                 data[base+10], data[base+11]);
    const vec4  edgeColor = vec4(data[base+12], data[base+13],
                                 data[base+14], data[base+15]);

    const vec3 axis  = normalize(hitPos - origin);
    const vec3 midPt = (origin + hitPos) * 0.5;
    const vec3 toEye = normalize(u.camPos - midPt);
    const vec3 side  = normalize(cross(axis, toEye)) * radius;

    const int ci    = gl_VertexIndex % 4;
    float t         = (ci >= 2) ? 1.0 : 0.0;
    float sSign     = (ci == 1 || ci == 2) ? 1.0 : -1.0;

    const vec3 wpos = mix(origin, hitPos, t) + side * sSign;
    gl_Position = u.proj * u.view * vec4(wpos, 1.0);

    vUV         = vec2(t, sSign);
    vBrightness = clamp(lifetime / 0.12, 0.0, 1.0);
    vCoreColor  = coreColor;
    vEdgeColor  = edgeColor;
}
