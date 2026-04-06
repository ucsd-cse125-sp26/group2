#version 450

layout(std140, set = 1, binding = 0) uniform VertexUniforms {
    mat4 transform;
} vu;

// Vertex format: Vertex_2f_4ub_2f (20 bytes)
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;  // UBYTE4_NORM
layout(location = 2) in vec2 in_obj;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_obj;

void main() {
    v_color     = in_color;
    v_obj       = in_obj;
    gl_Position = vu.transform * vec4(in_pos, 0.0, 1.0);
}
