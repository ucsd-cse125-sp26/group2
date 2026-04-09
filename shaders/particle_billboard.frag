#version 450

layout(location = 0) in  vec2  vUV;
layout(location = 1) in  vec4  vColor;
layout(location = 0) out vec4  outColor;

void main()
{
    // Soft circle: fade to transparent at radius 0.5 from centre
    float dist = length(vUV);
    float alpha = 1.0 - smoothstep(0.35, 0.5, dist);
    outColor = vColor * alpha;
}
