#version 450
layout(set = 2, binding = 0) uniform sampler2D sULTexture;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() { out_color = texture(sULTexture, v_uv); }
