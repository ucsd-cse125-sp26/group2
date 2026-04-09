#version 450

// SdfGlyphGPU (80 bytes = 20 floats):
//   [0..2]  worldPos.xyz  [3] size
//   [4..5]  uvMin.xy      [6..7] uvMax.xy
//   [8..11] color.rgba
//   [12..14] right.xyz    [15] _p0
//   [16..18] up.xyz       [19] _p1

layout(set = 0, binding = 0) readonly buffer GlyphData { float data[]; };

// World text uses full ParticleUniforms; HUD text uses HudUniforms.
// Both are pushed at set=1, binding=0.  The pipeline variant controls
// which uniform layout is active.  We define both and use a specialisation
// constant to pick at pipeline creation — but since SDL_GPU doesn't expose
// that easily, we just use the 176-byte ParticleUniforms for world text
// and the 16-byte HudUniforms (invScreenSize) for HUD text, which fits in
// the same slot.  Fragment shader is identical for both.

layout(set = 1, binding = 0) uniform Uniforms {
    // For world text:  first 128 bytes = view(64) + proj(64)
    // For HUD text:    first 8 bytes = invScreenSize.xy, rest unused
    mat4 view;
    mat4 proj;
    vec3 camPos;   float _p0;
    vec3 camRight; float _p1;
    vec3 camUp;    float _p2;
} u;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

const vec2 corners[4] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 1.0)
);

void main()
{
    const int stride = 20;
    const int base   = gl_InstanceIndex * stride;

    const vec3  origin = vec3(data[base+ 0], data[base+ 1], data[base+ 2]);
    const float size   = data[base+ 3];
    const vec2  uvMin  = vec2(data[base+ 4], data[base+ 5]);
    const vec2  uvMax  = vec2(data[base+ 6], data[base+ 7]);
    const vec4  color  = vec4(data[base+ 8], data[base+ 9],
                              data[base+10], data[base+11]);
    const vec3  right  = vec3(data[base+12], data[base+13], data[base+14]);
    const vec3  up     = vec3(data[base+16], data[base+17], data[base+18]);

    // Glyph aspect ratio from UV region
    const vec2 uvSize  = uvMax - uvMin;
    const float aspect = (uvSize.y > 0.0) ? uvSize.x / uvSize.y : 1.0;
    const float width  = size * aspect;

    const vec2 c    = corners[gl_VertexIndex % 4];
    const vec3 wpos = origin
        + right * c.x * width
        + up    * c.y * size;

    gl_Position = u.proj * u.view * vec4(wpos, 1.0);
    vUV   = mix(uvMin, uvMax, c);
    vColor = color;
}
