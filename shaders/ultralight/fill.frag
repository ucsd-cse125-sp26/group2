#version 450

layout(set = 2, binding = 0) uniform sampler2D sTexture1;
layout(set = 2, binding = 1) uniform sampler2D sTexture2;

// std140 alignment: State(16) + Transform(64) + Scalar4(32) + Vector(128)
//                   + ClipSize+pad(16) + Clip[8](512) = 768 bytes
layout(std140, set = 3, binding = 0) uniform FragUniforms {
    vec4  State;        // [0]=time [1]=vp_w [2]=vp_h [3]=unused
    mat4  Transform;    // (vertex-stage only; here for layout parity)
    vec4  Scalar4[2];   // 8 scalar params as 2×vec4
    vec4  Vector[8];    // 8 vec4 params
    uint  ClipSize;
    uint  _pad0, _pad1, _pad2;
    mat4  Clip[8];
} fu;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_tex;
layout(location = 2) in vec2 v_obj;
layout(location = 3) in vec4 v_data0;  // [0]=FillType [1]=ShaderType
layout(location = 4) in vec4 v_data1;
layout(location = 5) in vec4 v_data2;
layout(location = 6) in vec4 v_data3;
layout(location = 7) in vec4 v_data4;
layout(location = 8) in vec4 v_data5;
layout(location = 9) in vec4 v_data6;

layout(location = 0) out vec4 out_color;

// Signed distance to a rounded rectangle edge.
float roundRectSDF(vec2 p, vec2 half_size, float r) {
    vec2 q = abs(p) - half_size + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    int fill_type = int(v_data0.x + 0.5);

    if (fill_type == 0) {
        // Solid color — vertex color (premultiplied)
        out_color = v_color;

    } else if (fill_type == 1) {
        // Image / texture fill
        out_color = texture(sTexture1, v_tex) * v_color;

    } else if (fill_type == 2) {
        // Pattern fill (tiled texture)
        vec2 tile_uv = fract(v_obj / fu.Vector[0].xy);
        out_color = texture(sTexture1, tile_uv) * v_color;

    } else if (fill_type == 3) {
        // Linear gradient between Vector[0] (color0) and Vector[1] (color1)
        // gradient direction encoded in Vector[2].xy (start) and Vector[3].xy (end)
        vec2 grad_start = fu.Vector[2].xy;
        vec2 grad_end   = fu.Vector[3].xy;
        vec2 d = grad_end - grad_start;
        float len_sq = dot(d, d);
        float t = (len_sq > 0.0)
            ? clamp(dot(v_obj - grad_start, d) / len_sq, 0.0, 1.0)
            : 0.0;
        out_color = mix(fu.Vector[0], fu.Vector[1], t);

    } else if (fill_type == 4) {
        // Radial gradient: Vector[0]=center/radius, Vector[1]=c0, Vector[2]=c1
        float r = fu.Vector[0].z;
        float t = (r > 0.0) ? clamp(length(v_obj - fu.Vector[0].xy) / r, 0.0, 1.0) : 0.0;
        out_color = mix(fu.Vector[1], fu.Vector[2], t);

    } else if (fill_type == 5) {
        // Box shadow (exterior): Scalar4[0].x = shadow opacity
        out_color = v_color * fu.Scalar4[0].x;

    } else if (fill_type == 6) {
        // Box shadow (inner): straight vertex color
        out_color = v_color;

    } else if (fill_type == 7) {
        // SDF alpha-tested glyph — Scalar4[0].x = weight
        float d     = texture(sTexture1, v_tex).r;
        float w     = fu.Scalar4[0].x;
        float alpha = smoothstep(0.5 - w, 0.5 + w, d);
        out_color   = vec4(v_color.rgb, v_color.a * alpha);

    } else if (fill_type == 8) {
        // Rounded rectangle with anti-aliased edge
        // Vector[0] = (half_width, half_height, radius, unused)
        float d     = roundRectSDF(v_obj, fu.Vector[0].xy, fu.Vector[0].z);
        float alpha = clamp(0.5 - d, 0.0, 1.0);
        out_color   = vec4(v_color.rgb, v_color.a * alpha);

    } else {
        // Fallback: solid vertex color
        out_color = v_color;
    }
}
