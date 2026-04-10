#version 450

layout(location = 0) in  vec2  vUV;
layout(location = 1) in  float vOpacity;

layout(set = 2, binding = 0) uniform sampler2D decalAtlas;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 tex = texture(decalAtlas, vUV);

    // tex.a encodes the circular mask (0 outside, 1 inside) + alpha-faded scorch.
    // Multiply by per-decal fade opacity (ages toward 0 over time).
    float alpha = tex.a * vOpacity;

    // Discard near-invisible fragments so depth-biased quads don't waste fill rate.
    if (alpha < 0.01)
        discard;

    outColor = vec4(tex.rgb, alpha);
}
