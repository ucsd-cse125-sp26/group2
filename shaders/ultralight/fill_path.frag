#version 450

layout(set = 2, binding = 0) uniform sampler2D sPattern;

layout(std140, set = 3, binding = 0) uniform FragUniforms {
    vec4 State;
    mat4 Transform;
    vec4 Scalar4[2];
    vec4 Vector[8];
    uint ClipSize;
    uint _pad0, _pad1, _pad2;
    mat4 Clip[8];
} fu;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_obj;

layout(location = 0) out vec4 out_color;

void main() {
    // Path fill: solid vertex color; texture used only when enabled.
    float alpha = texture(sPattern, v_obj).r;
    out_color   = vec4(v_color.rgb, v_color.a * alpha);
}
