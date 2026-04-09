#version 450

layout(location = 0) in  float vEdge;
layout(location = 1) in  vec4  vColor;
layout(location = 0) out vec4  outColor;

void main()
{
    float t = 1.0 - abs(vEdge);
    float glow = pow(t, 2.0);
    outColor = vColor * glow;
}
