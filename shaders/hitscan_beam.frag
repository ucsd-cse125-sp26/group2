#version 450

layout(location = 0) in  vec2  vUV;
layout(location = 1) in  float vBrightness;
layout(location = 2) in  vec4  vCoreColor;
layout(location = 3) in  vec4  vEdgeColor;
layout(location = 0) out vec4  outColor;

void main()
{
    float t         = 1.0 - abs(vUV.y);
    float intensity = pow(t, 2.5) * vBrightness;
    outColor = mix(vEdgeColor, vCoreColor, t) * intensity;
}
