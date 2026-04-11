#version 450

// TracerParticle layout (64 bytes = 16 floats):
//   [0..2]  tip.xyz
//   [3]     radius
//   [4..6]  tail.xyz
//   [7]     brightness
//   [8..11] coreColor rgba
//   [12..15] edgeColor rgba
//   [16]    lifetime  (+ 3 pad) — not needed in VS

layout(set = 0, binding = 0) readonly buffer TracerData { float data[]; };

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
    const int stride = 20; // floats (TracerParticle = 80 bytes)
    const int base   = gl_InstanceIndex * stride;

    const vec3  tip        = vec3(data[base+ 0], data[base+ 1], data[base+ 2]);
    const float radius     = data[base + 3];
    const vec3  tail       = vec3(data[base+ 4], data[base+ 5], data[base+ 6]);
    const float brightness = data[base + 7];
    const vec4  coreColor  = vec4(data[base+ 8], data[base+ 9],
                                  data[base+10], data[base+11]);
    const vec4  edgeColor  = vec4(data[base+12], data[base+13],
                                  data[base+14], data[base+15]);

    // Build camera-facing side vector perpendicular to streak axis
    const vec3 axis   = normalize(tip - tail);
    const vec3 midPt  = (tip + tail) * 0.5;
    const vec3 toEye  = normalize(u.camPos - midPt);
    const vec3 side   = normalize(cross(axis, toEye)) * radius;

    // 4 corners of the oriented quad:
    //  corner u=(0,±1) at tail, corner u=(1,±1) at tip
    // gl_VertexIndex % 4 → corner index
    const int ci = gl_VertexIndex % 4;
    // (tailSide-, tailSide+, tipSide+, tipSide-)
    float t   = (ci >= 2) ? 1.0 : 0.0;  // 0 = tail end, 1 = tip end
    float sSign = (ci == 1 || ci == 2) ? 1.0 : -1.0;

    const vec3 wpos = mix(tail, tip, t) + side * sSign;
    gl_Position = u.proj * u.view * vec4(wpos, 1.0);

    vUV         = vec2(t, sSign);
    vBrightness = brightness;
    vCoreColor  = coreColor;
    vEdgeColor  = edgeColor;
}
