#version 450

layout(set = 2, binding = 0) uniform sampler2D sTexture1;
layout(set = 2, binding = 1) uniform sampler2D sTexture2;

layout(std140, set = 3, binding = 0) uniform FragUniforms {
    vec4  State;
    mat4  Transform;
    vec4  Scalar4[2];
    vec4  Vector[8];
    uint  ClipSize;
    uint  _pad0, _pad1, _pad2;
    mat4  Clip[8];
} fu;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_tex;
layout(location = 2) in vec2 v_obj;
layout(location = 3) in vec4 v_data0;
layout(location = 4) in vec4 v_data1;
layout(location = 5) in vec4 v_data2;
layout(location = 6) in vec4 v_data3;
layout(location = 7) in vec4 v_data4;
layout(location = 8) in vec4 v_data5;
layout(location = 9) in vec4 v_data6;

layout(location = 0) out vec4 out_color;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define AA_WIDTH 0.354

// ---------------------------------------------------------------------------
// SDF helpers
// ---------------------------------------------------------------------------
float sdRect(vec2 p, vec2 size) {
    vec2 d = abs(p) - size;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// Signed distance to an ellipse (Inigo Quilez, MIT license)
float sdEllipse(vec2 p, vec2 ab) {
    if (abs(ab.x - ab.y) < 0.1)
        return length(p) - ab.x;
    p = abs(p);
    if (p.x > p.y) { p = p.yx; ab = ab.yx; }
    float l  = ab.y*ab.y - ab.x*ab.x;
    float m  = ab.x*p.x / l;
    float n  = ab.y*p.y / l;
    float m2 = m*m, n2 = n*n;
    float c  = (m2 + n2 - 1.0) / 3.0;
    float c3 = c*c*c;
    float q  = c3 + m2*n2*2.0;
    float d  = c3 + m2*n2;
    float g  = m + m*n2;
    float co;
    if (d < 0.0) {
        float h  = acos(q / c3) / 3.0;
        float s  = cos(h);
        float t  = sin(h) * sqrt(3.0);
        float rx = sqrt(-c*(s + t + 2.0) + m2);
        float ry = sqrt(-c*(s - t + 2.0) + m2);
        co = (ry + sign(l)*rx + abs(g)/(rx*ry) - m) / 2.0;
    } else {
        float h2 = 2.0*m*n*sqrt(d);
        float s  = sign(q + h2) * pow(abs(q + h2), 1.0/3.0);
        float u  = sign(q - h2) * pow(abs(q - h2), 1.0/3.0);
        float rx = -s - u - c*4.0 + 2.0*m2;
        float ry = (s - u) * sqrt(3.0);
        float rm = sqrt(rx*rx + ry*ry);
        float h3 = ry / sqrt(rm - rx);
        co = (h3 + 2.0*g/rm - m) / 2.0;
    }
    float si = sqrt(1.0 - co*co);
    vec2  r  = vec2(ab.x*co, ab.y*si);
    return length(r - p) * sign(p.y - r.y);
}

// Signed distance to a rounded rectangle.
// p    = point relative to rect center (pixels)
// size = full width/height of the rect (pixels)
// rx   = per-corner X radii (TL, TR, BR, BL)
// ry   = per-corner Y radii
float sdRoundRect(vec2 p, vec2 size, vec4 rx, vec4 ry) {
    size *= 0.5;
    vec2 corner, local;
    // Top-left
    corner = vec2(-size.x + rx.x, -size.y + ry.x);
    local  = p - corner;
    if (dot(rx.x, ry.x) > 0.0 && p.x < corner.x && p.y <= corner.y)
        return sdEllipse(local, vec2(rx.x, ry.x));
    // Top-right
    corner = vec2( size.x - rx.y, -size.y + ry.y);
    local  = p - corner;
    if (dot(rx.y, ry.y) > 0.0 && p.x >= corner.x && p.y <= corner.y)
        return sdEllipse(local, vec2(rx.y, ry.y));
    // Bottom-right
    corner = vec2( size.x - rx.z,  size.y - ry.z);
    local  = p - corner;
    if (dot(rx.z, ry.z) > 0.0 && p.x >= corner.x && p.y >= corner.y)
        return sdEllipse(local, vec2(rx.z, ry.z));
    // Bottom-left
    corner = vec2(-size.x + rx.w,  size.y - ry.w);
    local  = p - corner;
    if (dot(rx.w, ry.w) > 0.0 && p.x < corner.x && p.y > corner.y)
        return sdEllipse(local, vec2(rx.w, ry.w));
    return sdRect(p, size);
}

// ---------------------------------------------------------------------------
// Antialiasing helpers
// ---------------------------------------------------------------------------
float antialias(float d, float width, float median) {
    return smoothstep(median - width, median + width, d);
}

float antialias2(float d) {
    return smoothstep(-AA_WIDTH, AA_WIDTH, d);
}

float innerStroke(float stroke_width, float d) {
    return min(antialias(-d, AA_WIDTH, 0.0),
               1.0 - antialias(-d, AA_WIDTH, stroke_width));
}

// ---------------------------------------------------------------------------
// Misc helpers
// ---------------------------------------------------------------------------
vec2 transformAffine(vec2 val, vec2 a, vec2 b, vec2 c) {
    return val.x * a + val.y * b + c;
}

// Unpack two uint16 values stored side-by-side in each float component.
void Unpack(vec4 x, out vec4 a, out vec4 b) {
    const float s = 65536.0;
    a = floor(x / s);
    b = floor(x - a * s);
}

vec4 blendOver(vec4 src, vec4 dst) {
    return vec4(src.rgb + dst.rgb * (1.0 - src.a),
                src.a  + dst.a  * (1.0 - src.a));
}

// ---------------------------------------------------------------------------
// Fill type 0 — solid color
// ---------------------------------------------------------------------------
void fillSolid() {
    out_color = v_color;
}

// ---------------------------------------------------------------------------
// Fill type 1 — image (texture * vertex color)
// ---------------------------------------------------------------------------
void fillImage() {
    out_color = texture(sTexture1, v_tex) * v_color;
}

// ---------------------------------------------------------------------------
// Fill type 2 — pattern (tiled image)
// ---------------------------------------------------------------------------
void fillPatternImage() {
    out_color = texture(sTexture1, fract(v_tex)) * v_color;
}

// ---------------------------------------------------------------------------
// Fill type 7 — filled rounded rectangle (with optional stroke)
// ---------------------------------------------------------------------------
void fillRoundedRect() {
    vec2 p    = v_tex;
    vec2 size = v_data0.zw;
    p = (p - 0.5) * size;               // map UV [0,1] → pixel coords centered
    float d   = sdRoundRect(p, size, v_data1, v_data2);
    float aa  = antialias(-d, AA_WIDTH, 0.0);
    out_color = v_color * aa;

    float stroke_width = v_data3.x;
    if (stroke_width > 0.0) {
        float sa     = innerStroke(stroke_width, d);
        vec4  stroke = v_data4 * sa;
        out_color    = blendOver(stroke, out_color);
    }
}

// ---------------------------------------------------------------------------
// Fill type 8 — box shadow (outer or inset)
// ---------------------------------------------------------------------------
void fillBoxShadow() {
    vec2  p          = v_obj;
    bool  inset      = bool(uint(v_data0.y + 0.5));
    float radius     = v_data0.z;
    vec2  origin     = v_data1.xy;
    vec2  size       = v_data1.zw;
    vec2  clip_orig  = v_data4.xy;
    vec2  clip_size  = v_data4.zw;

    float sdClip = sdRoundRect(p - clip_orig, clip_size, v_data5, v_data6);
    float sdBox  = sdRoundRect(p - origin,    size,      v_data2, v_data3);

    float clip = inset ? -sdBox  : sdClip;
    float dist = inset ? -sdClip : sdBox;

    if (clip < 0.0) {
        out_color = vec4(0.0);
        return;
    }

    float alpha = (radius >= 1.0)
        ? pow(antialias(-dist, radius * 2.0 + 0.2, 0.0), 1.9)
              * 3.3 / pow(radius * 1.2, 0.15)
        : antialias(-dist, AA_WIDTH, inset ? -1.0 : 1.0);

    alpha     = clamp(alpha, 0.0, 1.0) * v_color.a;
    out_color = vec4(v_color.rgb * alpha, alpha);
}

// ---------------------------------------------------------------------------
// Fill type 11 — SDF glyph (single-channel font atlas + luma-correction LUT)
//
// sTexture1 = R8 font atlas  (coverage/SDF values at v_tex UV)
// sTexture2 = R8 luma LUT    (alpha × fill_color_luma → gamma-corrected alpha)
// v_data0.y = precomputed luminance of the fill color (0=black text, 1=white)
// ---------------------------------------------------------------------------
void fillGlyph() {
    float alpha = texture(sTexture1, v_tex).r * v_color.a;
    alpha = clamp(alpha, 0.0, 1.0);
    float fill_color_luma  = v_data0.y;
    float corrected_alpha  = texture(sTexture2, vec2(alpha, fill_color_luma)).r;
    out_color = vec4(v_color.rgb * corrected_alpha, corrected_alpha);
}

// ---------------------------------------------------------------------------
// Clip masking — applied after every fill
// ---------------------------------------------------------------------------
void applyClip() {
    for (uint i = 0u; i < fu.ClipSize; i++) {
        mat4 data     = fu.Clip[i];
        vec2 origin   = data[0].xy;
        vec2 size     = data[0].zw;
        vec4 radii_x, radii_y;
        Unpack(data[1], radii_x, radii_y);
        bool inverse  = bool(data[3].z > 0.5);
        vec2 p        = v_obj;
        p = transformAffine(p, data[2].xy, data[2].zw, data[3].xy);
        p -= origin;
        float d_clip  = sdRoundRect(p, size, radii_x, radii_y)
                        * (inverse ? -1.0 : 1.0);
        float alpha   = antialias2(-d_clip);
        out_color     = vec4(out_color.rgb * alpha, out_color.a * alpha);
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
void main() {
    uint fill_type = uint(v_data0.x + 0.5);

    switch (fill_type) {
    case  0u: fillSolid();        break;
    case  1u: fillImage();        break;
    case  2u: fillPatternImage(); break;
    case  7u: fillRoundedRect();  break;
    case  8u: fillBoxShadow();    break;
    case 11u: fillGlyph();        break;
    default:
        // Unhandled fill type — output vertex color as best-effort fallback.
        out_color = v_color;
        break;
    }

    applyClip();
}
