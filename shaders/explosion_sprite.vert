/// @file explosion_sprite.vert
/// @brief Animated textured explosion sprite vertex shader.
#version 450

layout(set = 0, binding = 0) readonly buffer SpriteData { float data[]; };

layout(set = 1, binding = 0) uniform ParticleUniforms {
    mat4  view;
    mat4  proj;
    vec3  camPos;   float _p0;
    vec3  camRight; float _p1;
    vec3  camUp;    float _p2;
} u;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec4 vAnim;
layout(location = 3) out vec4 vShape;

const vec2 corners[4] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

void main()
{
    const int stride = 24;
    const int base = gl_InstanceIndex * stride;

    vec3 pos = vec3(data[base + 0], data[base + 1], data[base + 2]);
    float size = data[base + 3];
    vec3 vel = vec3(data[base + 4], data[base + 5], data[base + 6]);
    float rotation = data[base + 7];
    vec4 color = vec4(data[base + 8], data[base + 9], data[base + 10], data[base + 11]);
    vec4 age = vec4(data[base + 12], data[base + 13], data[base + 14], data[base + 15]);
    vec4 anim = vec4(data[base + 16], data[base + 17], data[base + 18], data[base + 19]);
    vec4 shape = vec4(data[base + 20], data[base + 21], data[base + 22], data[base + 23]);

    vec2 c = corners[gl_VertexIndex % 4];
    float s = sin(rotation);
    float co = cos(rotation);
    vec2 rc = vec2(c.x * co - c.y * s, c.x * s + c.y * co);

    vec3 right = u.camRight;
    vec3 up = u.camUp;
    float speed = length(vel);
    if (shape.x > 1.05 && speed > 8.0) {
        vec3 dir = vel / speed;
        vec3 toEye = normalize(u.camPos - pos);
        right = normalize(cross(dir, toEye));
        up = dir;
        rc.y *= shape.x;
    }

    vec3 wpos = pos + right * rc.x * size + up * rc.y * size;
    gl_Position = u.proj * u.view * vec4(wpos, 1.0);

    vUV = c * 0.5 + 0.5;
    vColor = color;
    vAnim = vec4(age.x, age.y, anim.x + floor(mod(age.x * anim.z, max(anim.y, 1.0))), anim.w);
    vShape = shape;
}
