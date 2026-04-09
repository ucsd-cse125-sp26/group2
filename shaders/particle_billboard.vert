#version 450

// BillboardParticle layout (48 bytes = 12 floats):
//   [0..2]  pos.xyz   [3] size
//   [4..7]  color.rgba
//   [8..10] vel.xyz   [11] lifetime

layout(set = 0, binding = 0) readonly buffer ParticleData { float data[]; };

layout(set = 1, binding = 0) uniform ParticleUniforms {
    mat4  view;
    mat4  proj;
    vec3  camPos;   float _p0;
    vec3  camRight; float _p1;
    vec3  camUp;    float _p2;
} u;

layout(location = 0) out vec2  vUV;
layout(location = 1) out vec4  vColor;
layout(location = 2) out float vSpeedNorm; // 0 = slow/circle, 1 = fast/streak

// Quad corners indexed by gl_VertexIndex % 4
const vec2 corners[4] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

void main()
{
    const int stride = 12;
    const int base   = gl_InstanceIndex * stride;

    const vec3  pos   = vec3(data[base+0], data[base+1], data[base+2]);
    const float size  = data[base+3];
    const vec4  color = vec4(data[base+4], data[base+5], data[base+6], data[base+7]);
    const vec3  vel   = vec3(data[base+8], data[base+9], data[base+10]);

    const float speed     = length(vel);
    const float speedNorm = clamp(speed / 400.0, 0.0, 1.0); // 400 u/s = full streak

    const vec2  c   = corners[gl_VertexIndex % 4];
    vec3 wpos;

    if (speed > 20.0) {
        // Orient quad along velocity: forms an elongated spark streak
        const vec3  velDir  = vel / speed;
        const vec3  toEye   = normalize(u.camPos - pos);
        const vec3  sideDir = normalize(cross(velDir, toEye));

        // Streak: longer than wide — length scales with speed
        const float halfLen  = size * (1.5 + speedNorm * 3.5);
        const float halfWide = size * 0.25;

        // c.x maps to along-velocity axis, c.y to cross axis
        wpos = pos + velDir * c.x * halfLen + sideDir * c.y * halfWide;
    } else {
        // Slow / stationary → spherical billboard
        wpos = pos + u.camRight * c.x * size + u.camUp * c.y * size;
    }

    gl_Position = u.proj * u.view * vec4(wpos, 1.0);
    vUV         = c;
    vColor      = color;
    vSpeedNorm  = speedNorm;
}
