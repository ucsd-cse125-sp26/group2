/// @file sdf_text.frag
/// @brief SDF text fragment shader with anti-aliased edge smoothing.
#version 450

layout(location = 0) in  vec2 vUV;
layout(location = 1) in  vec4 vColor;

/// @brief SDF font atlas texture.
layout(set = 2, binding = 0) uniform sampler2D sdfAtlas;

layout(location = 0) out vec4 outColor;

void main()
{
    float sdf   = texture(sdfAtlas, vUV).r;
    float w     = fwidth(sdf) * 0.7;
    float alpha = smoothstep(0.5 - w, 0.5 + w, sdf);
    outColor    = vec4(vColor.rgb, vColor.a * alpha);
}
