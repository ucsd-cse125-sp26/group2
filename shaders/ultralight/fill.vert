#version 450

// Uniform block for vertex stage — slot 0 (SDL_PushGPUVertexUniformData)
layout(std140, set = 1, binding = 0) uniform VertexUniforms {
    mat4 transform;
} vu;

// Vertex format: Vertex_2f_4ub_2f_2f_28f (140 bytes)
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;    // UBYTE4_NORM → [0,1]
layout(location = 2) in vec2 in_tex;
layout(location = 3) in vec2 in_obj;
layout(location = 4) in vec4 in_data0;
layout(location = 5) in vec4 in_data1;
layout(location = 6) in vec4 in_data2;
layout(location = 7) in vec4 in_data3;
layout(location = 8) in vec4 in_data4;
layout(location = 9) in vec4 in_data5;
layout(location = 10) in vec4 in_data6;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_tex;
layout(location = 2) out vec2 v_obj;
layout(location = 3) out vec4 v_data0;
layout(location = 4) out vec4 v_data1;
layout(location = 5) out vec4 v_data2;
layout(location = 6) out vec4 v_data3;
layout(location = 7) out vec4 v_data4;
layout(location = 8) out vec4 v_data5;
layout(location = 9) out vec4 v_data6;

void main() {
    v_color = in_color;
    v_tex   = in_tex;
    v_obj   = in_obj;
    v_data0 = in_data0;
    v_data1 = in_data1;
    v_data2 = in_data2;
    v_data3 = in_data3;
    v_data4 = in_data4;
    v_data5 = in_data5;
    v_data6 = in_data6;
    gl_Position = vu.transform * vec4(in_pos, 0.0, 1.0);
}
