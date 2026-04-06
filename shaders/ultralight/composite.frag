#version 450

layout(set = 2, binding = 0) uniform sampler2D sULTexture;

// UV rect of the actual content within the (possibly padded) RTT texture.
layout(std140, set = 3, binding = 0) uniform CompositeUniforms {
    vec4 uvRect; // (left, top, right, bottom)
} u;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 uv = mix(u.uvRect.xy, u.uvRect.zw, v_uv);
    out_color = texture(sULTexture, uv);
}
