#version 450

layout(location = 0) in  vec2  vUV;
layout(location = 1) in  float vBrightness;
layout(location = 2) in  vec4  vCoreColor;
layout(location = 3) in  vec4  vEdgeColor;
layout(location = 0) out vec4  outColor;

void main()
{
    float dist = abs(vUV.y);
    float core = 1.0 - smoothstep(0.0, 0.18, dist);  // tight white-hot core
    float glow = 1.0 - smoothstep(0.0, 1.0,  dist);  // broad orange glow
    outColor = mix(vEdgeColor, vCoreColor, core) * glow * vBrightness;
}
