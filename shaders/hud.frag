/// @file hud.frag
/// @brief HUD fragment shader — branches on texMode for solid, SDF text,
///        sprite, SDF rounded rect, or vignette.
///
/// Output is **premultiplied alpha**: the pipeline's blend state is set up
/// to expect (rgb·a, a) here, so we always emit `vec4(rgb*alpha, alpha)`.
/// Premultiplied alpha is required for correct MSAA resolve (averaging
/// straight alpha across sub-samples produces dark fringes around edges)
/// and for correct compositing through the offscreen HUD target onto the
/// swapchain.
///
/// SDF text (mode 1) uses Valve-style screen-space distance AA: we derive
/// the SDF spread in screen pixels via UV derivatives and the known
/// `k_pxRange` baked into the atlas, then alpha = clamp(distance + 0.5).
/// This produces edges that stay exactly 1 pixel wide regardless of the
/// glyph's render size, killing the "soft mush at small sizes" failure
/// mode that `fwidth(sdf)`-based AA had.
#version 450

layout(location = 0) in vec2  vUV;
layout(location = 1) in vec4  vColor;
layout(location = 2) in float vTexMode;
layout(location = 3) in vec3  vShapeData;

layout(set = 2, binding = 0) uniform sampler2D sdfAtlas;
layout(set = 2, binding = 1) uniform sampler2D iconAtlas;

layout(location = 0) out vec4 outColor;

/// @brief SDF spread in atlas pixels — must match `SdfAtlas::k_spread`.
/// The shader uses this to compute the screen-space distance band.
const float k_pxRange = 12.0;

/// @brief Pack a straight-alpha (rgb, a) color into a premultiplied output.
vec4 premul(vec3 rgb, float a) { return vec4(rgb * a, a); }

/// @brief Compute the screen-space pixel range of the SDF transition band.
/// Standard Valve recipe: convert known atlas-pixel padding to screen
/// pixels via UV-space derivatives. Clamped at 1 so very-large text still
/// gets at least one pixel of AA (no aliased over-sharpening).
float screenPxRange()
{
    vec2 unitRange   = vec2(k_pxRange) / vec2(textureSize(sdfAtlas, 0));
    vec2 screenRange = vec2(1.0) / fwidth(vUV);
    return max(0.5 * dot(unitRange, screenRange), 1.0);
}

void main()
{
    int mode = int(vTexMode + 0.5);

    if (mode == 1) {
        // SDF text — Valve-style screen-space distance AA.
        float sdf = texture(sdfAtlas, vUV).r;
        float pxRange = screenPxRange();

        // Signed distance to the edge in screen pixels.
        float screenPxDistance = pxRange * (sdf - 0.5);

        // Anti-aliased fill alpha: stays within ±0.5 px of the true edge.
        float fillAlpha = clamp(screenPxDistance + 0.5, 0.0, 1.0);

        // Outline strength is encoded into vShapeData.x by the CPU side:
        //   0.0 = no outline (clean text over panel chrome)
        //   1.0 = draw a thin dark outline (legibility over the world)
        // We avoid drawing the outline universally because adjacent glyphs'
        // outlines visibly overlap, mashing letters together — the
        // "ARC-9 / RDY look like they have a black background" complaint.
        float outlineAmount = clamp(vShapeData.x, 0.0, 1.0);

        // Outline distance: 0.5 px wider than the glyph edge on the outside.
        // Tighter than v1 (1 px) — at small text sizes a 1-px outline ate
        // the gap between adjacent glyphs entirely.
        const float kOutlinePxWidth = 0.5;
        float outlineDistance = screenPxDistance + kOutlinePxWidth * outlineAmount;
        float outlineAlpha    = clamp(outlineDistance + 0.5, 0.0, 1.0);

        // Composite: when outline is on, dark band sits behind the fill;
        // when off, the alpha is just the fill itself (no dark halo).
        vec3  rgb   = mix(vec3(0.0), vColor.rgb, fillAlpha);
        float alpha = mix(fillAlpha, outlineAlpha, outlineAmount) * vColor.a;
        outColor = premul(rgb, alpha);

    } else if (mode == 2) {
        // Sprite / icon — texture is RGBA straight-alpha; tint and convert.
        vec4 texel = texture(iconAtlas, vUV) * vColor;
        outColor = premul(texel.rgb, texel.a);

    } else if (mode == 3) {
        // SDF rounded rectangle (analytic distance, no atlas needed).
        vec2  halfExt = vShapeData.xy;
        float radius  = vShapeData.z;

        vec2  localPos = (vUV * 2.0 - 1.0) * halfExt;
        vec2  d = abs(localPos) - (halfExt - radius);
        float dist = min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - radius;

        // Anti-aliased edge — the analytic distance is already in pixels,
        // so a 1-pixel-wide fade window is just `clamp(-dist + 0.5)`.
        float alpha = clamp(0.5 - dist, 0.0, 1.0);
        outColor = premul(vColor.rgb, vColor.a * alpha);

    } else if (mode == 4) {
        // Vignette: radial edge gradient (full-screen quad).
        vec2  uv   = vUV * 2.0 - 1.0;
        float dist = length(uv);
        float vig  = smoothstep(0.35, 1.4, dist);
        vig = vig * vig;
        outColor = premul(vColor.rgb, vColor.a * vig);

    } else {
        // Mode 0: solid color.
        outColor = premul(vColor.rgb, vColor.a);
    }
}
