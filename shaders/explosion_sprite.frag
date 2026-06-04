/// @file explosion_sprite.frag
/// @brief Animated textured explosion sprite fragment shader.
#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec4 vAnim;
layout(location = 3) in vec4 vShape;

layout(set = 2, binding = 0) uniform sampler2D explosionAtlas;

layout(location = 0) out vec4 outColor;

vec4 sampleFrame(float frame)
{
    const float cells = 8.0;
    float f = clamp(frame, 0.0, 63.0);
    vec2 cell = vec2(mod(f, cells), floor(f / cells));
    vec2 uv = (cell + clamp(vUV, vec2(0.002), vec2(0.998))) / cells;
    return texture(explosionAtlas, uv);
}

void main()
{
    float age = clamp(vAnim.x / max(vAnim.y, 0.001), 0.0, 1.0);
    float material = vAnim.w;
    vec2 centered = vUV * 2.0 - 1.0;
    float radial = length(centered);
    vec4 tex = sampleFrame(vAnim.z);

    float edge = 1.0 - smoothstep(0.78, 1.08, radial);
    float alpha = tex.a * vColor.a * edge;
    vec3 rgb = tex.rgb * vColor.rgb;

    if (material < 0.5) {
        float core = 1.0 - smoothstep(0.0, 0.32, radial);
        rgb = mix(rgb, vec3(1.0, 0.86, 0.58), core * (1.0 - age));
        alpha *= 1.0 - smoothstep(0.76, 1.0, age);
    } else if (material < 1.5) {
        float blackPoint = smoothstep(0.0, 0.72, age);
        rgb = max(rgb - blackPoint * 0.18, vec3(0.0));
        alpha *= smoothstep(0.0, 0.10, age) * (1.0 - smoothstep(0.78, 1.0, age));
    } else if (material < 2.5) {
        float dissolve = smoothstep(0.08, 0.72, tex.a + vShape.y * 0.10 - age * 0.16);
        rgb *= mix(0.70, 1.16, tex.a);
        alpha *= dissolve * smoothstep(0.0, 0.12, age) * (1.0 - smoothstep(0.70, 1.0, age));
    } else if (material < 3.5) {
        rgb *= 1.4;
        alpha *= 1.0 - smoothstep(0.35, 1.0, age);
    } else {
        float lick = smoothstep(0.03, 0.70, tex.a) * (1.0 - smoothstep(0.65, 1.0, age));
        rgb = mix(rgb * 0.85, vec3(1.0, 0.28, 0.03), 0.40);
        alpha *= lick;
    }

    if (alpha < 0.006)
        discard;

    outColor = vec4(rgb * alpha, alpha);
}
