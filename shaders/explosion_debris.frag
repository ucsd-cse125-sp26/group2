/// @file explosion_debris.frag
/// @brief Bright streak fragment shader for explosion sparks and embers.
#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vAge;
layout(location = 0) out vec4 outColor;

void main()
{
    float crossFade = 1.0 - smoothstep(0.0, 1.0, abs(vUV.y));
    float tipFade = smoothstep(-1.0, 0.25, vUV.x);
    float core = 1.0 - smoothstep(0.0, 0.22, abs(vUV.y));
    float alpha = crossFade * tipFade * vColor.a * (1.0 - smoothstep(0.62, 1.0, vAge));
    vec3 hot = vColor.rgb + vec3(0.45, 0.30, 0.08) * core;
    if (alpha < 0.01)
        discard;
    outColor = vec4(hot * alpha, alpha);
}
