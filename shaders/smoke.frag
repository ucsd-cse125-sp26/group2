#version 450

layout(location = 0) in  vec2  vUV;
layout(location = 1) in  vec4  vColor;
layout(location = 2) in  float vAge;

layout(set = 2, binding = 0) uniform sampler2D smokeNoise;

layout(location = 0) out vec4 outColor;

void main()
{
    vec2  noiseUV  = vUV * 1.8 + vec2(vAge * 0.05, vAge * 0.03);
    float n        = texture(smokeNoise, noiseUV * 0.5 + 0.5).r;
    float mask     = smoothstep(0.35, 0.65, n);
    float radial   = 1.0 - smoothstep(0.0, 0.5, length(vUV));
    outColor = vColor * mask * radial;
}
