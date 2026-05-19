/// @file fire_flipbook.frag
/// @brief HDR atlas sampling for the generated fire flipbook preview.
#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in float vOpacity;

layout(set = 2, binding = 0) uniform sampler2D fireFlipbookAtlas;

layout(set = 3, binding = 0) uniform FlipbookFragmentUniforms {
    vec4 atlas; // x = columns, y = rows, z = current frame, w = next frame
    vec4 anim;  // x = frame blend
} f;

layout(location = 0) out vec4 outColor;

vec2 atlasUV(float frameIndex, vec2 localUV)
{
    float cols = max(f.atlas.x, 1.0);
    float rows = max(f.atlas.y, 1.0);
    float col = mod(frameIndex, cols);
    float row = floor(frameIndex / cols);
    vec2 atlasSize = vec2(textureSize(fireFlipbookAtlas, 0));
    vec2 tileSize = atlasSize / vec2(cols, rows);
    vec2 texelInset = 0.5 / max(tileSize, vec2(1.0));
    vec2 tileUV = vec2(localUV.x, 1.0 - localUV.y);
    tileUV = mix(texelInset, vec2(1.0) - texelInset, clamp(tileUV, 0.0, 1.0));
    return (vec2(col, row) + tileUV) / vec2(cols, rows);
}

void main()
{
    vec4 a = texture(fireFlipbookAtlas, atlasUV(f.atlas.z, vUV));
    vec4 b = texture(fireFlipbookAtlas, atlasUV(f.atlas.w, vUV));
    vec4 c = mix(a, b, clamp(f.anim.x, 0.0, 1.0)) * clamp(vOpacity, 0.0, 1.0);
    if (c.a <= 0.002)
        discard;
    outColor = c;
}
