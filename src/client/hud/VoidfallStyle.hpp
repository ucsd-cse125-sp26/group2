/// @file VoidfallStyle.hpp
/// @brief VOIDFALL HUD design tokens — colors, sizes, and shared helpers.
///
/// Mirrors the design system from the VOIDFALL HUD prototype:
/// - Warm near-black base, amber primary, cyan shield, warm red HP/damage.
/// - 1 px hairlines, mil-spec corner brackets on framed elements.
/// - JetBrains-Mono-style telemetry feel (tabular numerals, all-caps labels).
///
/// Colors are converted from the prototype's `oklch()` tokens to linear sRGB-ish
/// values that match perceptual intent within the existing HudColor (linear,
/// straight-alpha) plumbing — they're rendered straight through HudContext into
/// the existing tone-mapped HUD framebuffer, so the conversions are tuned by
/// eye to feel right against the same backgrounds the prototype assumed.

#pragma once

#include "HudContext.hpp"
#include "HudTypes.hpp"

#include <algorithm>

namespace voidfall
{

// ── Palette (matches prototype's CSS custom properties) ─────────────────────

/// @brief Prototype palette sampled from hud_proto.png / supplied spec.
constexpr HudColor k_primary{0.356863f, 0.949020f, 1.0f, 1.0f};         // Hologram cyan #5BF2FF
constexpr HudColor k_secondary{0.298039f, 0.901961f, 1.0f, 1.0f};       // Gauge cyan #4CE6FF
constexpr HudColor k_tertiary{0.639216f, 0.952941f, 1.0f, 1.0f};        // Ice highlight #A3F3FF
constexpr HudColor k_quaternary{0.086275f, 0.086275f, 0.180392f, 1.0f}; // Slate violet #16162E

constexpr HudColor k_bgVoid{k_quaternary.r, k_quaternary.g, k_quaternary.b, 0.78f};
/// @brief Standard panel fill — translucent glass base, reinforced by glow
/// layers in drawPanel() for readability without a flat sticker look.
constexpr HudColor k_bgPanel{0.09f, 0.08f, 0.18f, 0.46f};
/// @brief Solid panel — used for hero callouts.
constexpr HudColor k_bgPanelSolid{0.11f, 0.09f, 0.20f, 0.68f};
/// @brief Inset bar background.
constexpr HudColor k_bgInset{0.04f, 0.04f, 0.11f, 0.56f};

/// @brief Dim hairline — `oklch(0.32 0.015 60 / 0.6)`.
constexpr HudColor k_lineDim{k_secondary.r, k_secondary.g, k_secondary.b, 0.48f};
/// @brief Standard hairline — `oklch(0.40 0.018 60)`.
constexpr HudColor k_line{k_secondary.r, k_secondary.g, k_secondary.b, 0.88f};
/// @brief Bright hairline — `oklch(0.55 0.02 60)`.
constexpr HudColor k_lineBright{k_tertiary.r, k_tertiary.g, k_tertiary.b, 1.0f};

/// @brief Dim text — bumped luminance from ~0.55 to ~0.78 so secondary
/// readouts (fire mode, "+reserve", "/mag", "[2] PULSAR") read clearly
/// against the panel chrome instead of fading into it.
constexpr HudColor k_textDim{0.74f, 0.83f, 0.94f, 1.0f};
/// @brief Standard text — bumped slightly toward white for body chrome.
constexpr HudColor k_text{k_tertiary.r, k_tertiary.g, k_tertiary.b, 1.0f};
/// @brief Brightest text — kept near-white for hero readouts.
constexpr HudColor k_textBright{k_tertiary.r, k_tertiary.g, k_tertiary.b, 1.0f};

/// @brief Primary amber — `oklch(0.80 0.165 75)`.
constexpr HudColor k_amber{1.0f, 0.839216f, 0.356863f, 1.0f}; // Gold #FFD65B
/// @brief Dimmer amber — `oklch(0.65 0.13 75)`.
constexpr HudColor k_amberDim{1.0f, 0.45f, 0.26f, 1.0f}; // Coral orange #FF7342
/// @brief Deep amber — `oklch(0.45 0.10 70)`.
constexpr HudColor k_amberDeep{0.86f, 0.54f, 0.16f, 1.0f};
/// @brief Amber glow tint — `oklch(0.80 0.165 75 / 0.15)`.
constexpr HudColor k_amberGlow{1.0f, 0.84f, 0.0f, 0.18f};
constexpr HudColor k_glassHighlight{k_tertiary.r, k_tertiary.g, k_tertiary.b, 0.18f};
constexpr HudColor k_glassShadow{k_quaternary.r, k_quaternary.g, k_quaternary.b, 0.28f};
constexpr HudColor k_scanline{k_tertiary.r, k_tertiary.g, k_tertiary.b, 0.045f};
constexpr HudColor k_chromaRed{1.0f, 0.18f, 0.10f, 0.16f};
constexpr HudColor k_chromaCyan{0.16f, 0.90f, 1.0f, 0.16f};

constexpr HudColor k_red{1.0f, 0.36f, 0.28f, 1.0f};
constexpr HudColor k_redBright{1.0f, 0.54f, 0.30f, 1.0f};
constexpr HudColor k_redDim{0.55f, 0.18f, 0.16f, 1.0f};
constexpr HudColor k_health{0.196078f, 0.803922f, 0.196078f, 1.0f};     // #32CD32
constexpr HudColor k_healthBright{0.0f, 1.0f, 0.498039f, 1.0f};         // #00FF7F

/// @brief Shield cyan — `oklch(0.80 0.10 220)`.
constexpr HudColor k_cyan{k_primary.r, k_primary.g, k_primary.b, 1.0f};
/// @brief Dim cyan — `oklch(0.55 0.08 220)`.
constexpr HudColor k_cyanDim{k_secondary.r, k_secondary.g, k_secondary.b, 1.0f};

/// @brief Status green — `oklch(0.78 0.16 145)`.
constexpr HudColor k_green{k_healthBright.r, k_healthBright.g, k_healthBright.b, 1.0f};
constexpr HudColor k_purple{0.64f, 0.40f, 1.0f, 1.0f};                 // #A366FF
constexpr HudColor k_purpleBright{0.86f, 0.62f, 1.0f, 1.0f};
constexpr HudColor k_infoBlue{0.26f, 0.46f, 0.60f, 1.0f};
constexpr HudColor k_yellow{1.0f, 0.827451f, 0.305882f, 1.0f};          // #FFD34E

/// @brief Pickup amber-trim background.
constexpr HudColor k_pickupBg{k_bgPanel.r, k_bgPanel.g, k_bgPanel.b, 0.74f};

// ── Weapon-type accent colors (Apex convention) ───────────────────────────
//
// Each weapon class has its own bright accent: AR teal, sniper purple,
// pistol green, SMG amber, shotgun red.  Used for the weapon-panel
// underline, fire-mode tag border, slot-tab top-edge, and slot-index pill
// background — so the player parses *what* they're holding by color
// before reading the name.
constexpr HudColor k_typeAr = k_primary;
constexpr HudColor k_typeSniper = k_tertiary;
constexpr HudColor k_typePistol = k_secondary;
constexpr HudColor k_typeSmg = k_primary;
constexpr HudColor k_typeShotgun = k_secondary;

/// @brief Map a weapon-type id (matching `WeaponType` integer order in
/// `ecs/components/WeaponState.hpp`: 0=Rifle, 1=Rocket, 2=RailGun,
/// 3=EnergyGun, 4-6=grenades) to its accent color.
constexpr HudColor weaponTypeAccent(int weaponId)
{
    switch (weaponId) {
    case 0:  // Rifle — full-auto AR
        return k_typeAr;
    case 1:  // Rocket — explosive, treat as shotgun-class
        return k_typeShotgun;
    case 2:  // RailGun — single-shot precision
        return k_typeSniper;
    case 3:  // EnergyGun — beam SMG-class
        return k_typeSmg;
    default: // grenades / unknown — fall back to generic amber
        return k_amber;
    }
}

/// @brief Apply alpha to a palette color (for fade-out).
constexpr HudColor withAlpha(HudColor c, float alpha)
{
    return HudColor{c.r, c.g, c.b, c.a * alpha};
}

// ── Mil-spec bracket corners ────────────────────────────────────────────────

/// @brief Draw four mil-spec L-shaped corner brackets around a rect.
///
/// Brackets sit slightly outside the rect (offset by `outset` px) and extend
/// `armLen` px along each edge.  Style: 1 px hairlines, amber by default.
inline void drawCornerBrackets(HudContext& ctx,
                               float x,
                               float y,
                               float w,
                               float h,
                               float armLen = 10.f,
                               float thickness = 1.f,
                               float outset = 0.f,
                               HudColor color = k_amber)
{
    const float bx = x - outset;
    const float by = y - outset;
    const float bw = w + outset * 2.f;
    const float bh = h + outset * 2.f;

    // Top-left.
    ctx.rect(bx, by, armLen, thickness, color);
    ctx.rect(bx, by, thickness, armLen, color);
    // Top-right.
    ctx.rect(bx + bw - armLen, by, armLen, thickness, color);
    ctx.rect(bx + bw - thickness, by, thickness, armLen, color);
    // Bottom-left.
    ctx.rect(bx, by + bh - thickness, armLen, thickness, color);
    ctx.rect(bx, by + bh - armLen, thickness, armLen, color);
    // Bottom-right.
    ctx.rect(bx + bw - armLen, by + bh - thickness, armLen, thickness, color);
    ctx.rect(bx + bw - thickness, by + bh - armLen, thickness, armLen, color);
}

/// @brief Draw a lightweight projected-glass material over a panel.
inline void drawGlassMaterial(HudContext& ctx,
                              float x,
                              float y,
                              float w,
                              float h,
                              HudColor fill,
                              HudColor border,
                              float thickness)
{
    const float edge = std::max(1.f, thickness);
    const float materialAlpha = std::clamp(std::max(fill.a, border.a), 0.f, 1.f);

    // Soft outer bloom and red/cyan edge offsets simulate projected optics.
    ctx.rectOutline(x - 3.f, y - 3.f, w + 6.f, h + 6.f, edge, withAlpha(border, 0.08f));
    ctx.rectOutline(x - 1.f, y - 1.f, w + 2.f, h + 2.f, edge, withAlpha(border, 0.16f));
    ctx.rectOutline(x - 1.f, y, w, h, edge, withAlpha(k_chromaRed, materialAlpha));
    ctx.rectOutline(x + 1.f, y, w, h, edge, withAlpha(k_chromaCyan, materialAlpha));

    // Frosted-glass approximation: translucent body plus subtle top glint and
    // inner shadow. True backdrop blur would require sampling the scene buffer.
    ctx.rect(x, y, w, h, fill);
    ctx.gradientRect(x,
                     y,
                     w,
                     std::max(1.f, h * 0.18f),
                     withAlpha(k_glassHighlight, 0.65f * materialAlpha),
                     HudColor{0.f, 0.f, 0.f, 0.f});
    ctx.rect(x,
             y + h - std::max(1.f, h * 0.20f),
             w,
             std::max(1.f, h * 0.20f),
             withAlpha(k_glassShadow, materialAlpha));

    ctx.pushClipRect(x, y, w, h);
    for (float yy = y + 2.f; yy < y + h; yy += 4.f)
        ctx.rect(x, yy, w, 1.f, withAlpha(k_scanline, materialAlpha));
    ctx.popClipRect();

    const float inset = std::min(3.f, std::min(w, h) * 0.08f);
    if (w > inset * 2.f && h > inset * 2.f)
        ctx.rectOutline(x + inset, y + inset, w - inset * 2.f, h - inset * 2.f, 1.f, withAlpha(border, 0.20f));

    ctx.rectOutline(x, y, w, h, edge, border);
}

/// @brief Draw a Voidfall panel: projected glass fill + light-emitting outline.
inline void drawPanel(HudContext& ctx,
                      float x,
                      float y,
                      float w,
                      float h,
                      HudColor fill = k_bgPanel,
                      HudColor border = k_line,
                      float thickness = 1.f)
{
    drawGlassMaterial(ctx, x, y, w, h, fill, border, thickness);
}

inline HudColor holoColorAtX(float x, float leftX, float width, HudColor leftColor, HudColor rightColor)
{
    const float t = std::clamp((x - leftX) / std::max(1.f, width), 0.f, 1.f);
    return HudColor{
        leftColor.r + (rightColor.r - leftColor.r) * t,
        leftColor.g + (rightColor.g - leftColor.g) * t,
        leftColor.b + (rightColor.b - leftColor.b) * t,
        leftColor.a + (rightColor.a - leftColor.a) * t,
    };
}

inline int chamferPoints(float* out, float x, float y, float w, float h, float tl, float tr, float br, float bl)
{
    tl = std::clamp(tl, 0.f, std::min(w, h) * 0.45f);
    tr = std::clamp(tr, 0.f, std::min(w, h) * 0.45f);
    br = std::clamp(br, 0.f, std::min(w, h) * 0.45f);
    bl = std::clamp(bl, 0.f, std::min(w, h) * 0.45f);
    const float points[] = {
        x + tl,
        y,
        x + w - tr,
        y,
        x + w,
        y + tr,
        x + w,
        y + h - br,
        x + w - br,
        y + h,
        x + bl,
        y + h,
        x,
        y + h - bl,
        x,
        y + tl,
    };
    for (int i = 0; i < 16; ++i)
        out[i] = points[i];
    return 8;
}

inline void drawChamferFill(HudContext& ctx,
                            float x,
                            float y,
                            float w,
                            float h,
                            float tl,
                            float tr,
                            float br,
                            float bl,
                            HudColor leftColor,
                            HudColor rightColor)
{
    float points[16]{};
    const int count = chamferPoints(points, x, y, w, h, tl, tr, br, bl);
    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    const HudColor center = holoColorAtX(cx, x, w, leftColor, rightColor);
    for (int i = 0; i < count; ++i) {
        const int j = (i + 1) % count;
        const float ax = points[i * 2 + 0];
        const float ay = points[i * 2 + 1];
        const float bx = points[j * 2 + 0];
        const float by = points[j * 2 + 1];
        ctx.triangleColors(cx,
                           cy,
                           center,
                           ax,
                           ay,
                           holoColorAtX(ax, x, w, leftColor, rightColor),
                           bx,
                           by,
                           holoColorAtX(bx, x, w, leftColor, rightColor));
    }
}

inline void drawChamferOutline(HudContext& ctx,
                               float x,
                               float y,
                               float w,
                               float h,
                               float tl,
                               float tr,
                               float br,
                               float bl,
                               float thickness,
                               HudColor color)
{
    float p[18]{};
    chamferPoints(p, x, y, w, h, tl, tr, br, bl);
    p[16] = p[0];
    p[17] = p[1];
    ctx.polyline(p, 9, thickness, color);
}

inline void drawHoloPanel(HudContext& ctx,
                          float x,
                          float y,
                          float w,
                          float h,
                          float cut = 18.f,
                          HudColor fillLeft = k_bgPanelSolid,
                          HudColor fillRight = k_bgPanel,
                          HudColor border = k_primary,
                          float thickness = 2.f)
{
    drawChamferOutline(ctx, x - 4.f, y - 4.f, w + 8.f, h + 8.f, cut, cut, cut, cut, thickness * 2.4f, withAlpha(border, 0.10f));
    drawChamferOutline(ctx, x - 2.f, y - 2.f, w + 4.f, h + 4.f, cut, cut, cut, cut, thickness * 1.6f, withAlpha(border, 0.20f));
    drawChamferFill(ctx, x, y, w, h, cut, cut, cut, cut, fillLeft, fillRight);
    ctx.pushClipRect(x, y, w, h);
    ctx.gradientRect(x, y, w, std::max(2.f, h * 0.24f), withAlpha(k_tertiary, 0.18f), HudColor{0.f, 0.f, 0.f, 0.f});
    for (float yy = y + 4.f; yy < y + h; yy += 6.f)
        ctx.rect(x, yy, w, 1.f, withAlpha(k_scanline, 0.75f));
    ctx.popClipRect();
    drawChamferOutline(ctx, x + 3.f, y + 3.f, w - 6.f, h - 6.f, std::max(0.f, cut - 3.f), std::max(0.f, cut - 3.f), std::max(0.f, cut - 3.f), std::max(0.f, cut - 3.f), 1.f, withAlpha(k_tertiary, 0.46f));
    drawChamferOutline(ctx, x, y, w, h, cut, cut, cut, cut, thickness, border);
}

/// @brief Draw a horizontal stat bar with a damage-trail ghost (solid fill).
///
/// Background fill, then a translucent ghost-trail rectangle (where the bar
/// was before the latest damage tick), then the live fill on top.  This is the
/// "white ghost draining behind the live red" effect from the design.
inline void drawTrailBar(HudContext& ctx,
                         float x,
                         float y,
                         float w,
                         float h,
                         float fill01,
                         float trail01,
                         HudColor fillColor,
                         HudColor trailColor = HudColor{k_tertiary.r, k_tertiary.g, k_tertiary.b, 0.45f},
                         HudColor bgColor = k_bgInset,
                         HudColor borderColor = k_lineDim)
{
    // Background.
    ctx.rect(x, y, w, h, bgColor);
    // Ghost trail (drained portion drawn first, so live fill overlays it).
    if (trail01 > fill01)
        ctx.rect(x, y, w * trail01, h, trailColor);
    // Live fill.
    if (fill01 > 0.f)
        ctx.rect(x, y, w * fill01, h, fillColor);
    // Hairline border.
    ctx.rectOutline(x, y, w, h, 1.f, borderColor);
}

/// @brief Lerp between two `HudColor`s — used to compute the *visible*
/// gradient endpoint when the bar is partially filled (so the fill always
/// represents the same slice of the underlying full-bar gradient instead of
/// stretching to the right end).
constexpr HudColor lerpColor(HudColor a, HudColor b, float t)
{
    return HudColor{
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t,
    };
}

/// @brief Draw a horizontal stat bar whose live fill is a left→right
/// gradient (e.g. health: deep red → orange-red, shield: dim cyan → cyan).
///
/// `fillLeft` and `fillRight` describe the colors for a *full* (100%) bar.
/// At partial fills the right endpoint of the visible portion is interpolated
/// to `fill01` along that gradient — which keeps the warm/bright tail at
/// 100% and the cool/dim head as the bar drains, matching the prototype's
/// `linear-gradient(90deg, red, orange-red)` CSS behavior.
inline void drawGradientTrailBar(HudContext& ctx,
                                 float x,
                                 float y,
                                 float w,
                                 float h,
                                 float fill01,
                                 float trail01,
                                 HudColor fillLeft,
                                 HudColor fillRight,
                                 HudColor trailColor = HudColor{k_tertiary.r, k_tertiary.g, k_tertiary.b, 0.45f},
                                 HudColor bgColor = k_bgInset,
                                 HudColor borderColor = k_lineDim)
{
    ctx.rect(x, y, w, h, bgColor);
    if (trail01 > fill01)
        ctx.rect(x, y, w * trail01, h, trailColor);
    if (fill01 > 0.f) {
        // Right endpoint of the visible fill = same point in the underlying
        // gradient, so the bar drains cleanly without color compression.
        const HudColor visibleRight = lerpColor(fillLeft, fillRight, fill01);
        ctx.gradientRect(x, y, w * fill01, h, fillLeft, visibleRight);
    }
    ctx.rectOutline(x, y, w, h, 1.f, borderColor);
}

/// @brief Draw a bordered "key tab" pill with a single character inside.
///
/// Used by equipment slots and pickup hints.  The text is centered inside the
/// box; sizing is based on the cap-height of the SDF font for visual balance.
inline void drawKeyTab(HudContext& ctx,
                       const char* key,
                       float x,
                       float y,
                       float fontSize,
                       float padX = 3.f,
                       float padY = 1.f,
                       HudColor bgColor = HudColor{k_quaternary.r, k_quaternary.g, k_quaternary.b, 0.45f},
                       HudColor borderColor = k_lineBright,
                       HudColor textColor = k_textDim)
{
    const float capH = fontSize * 0.72f;
    const float w = ctx.measureText(key, fontSize) + padX * 2.f;
    const float h = capH + padY * 2.f;
    ctx.rect(x, y, w, h, bgColor);
    ctx.rectOutline(x, y, w, h, 1.f, borderColor);
    ctx.text(key, x + w * 0.5f, y - fontSize * 0.28f + padY, fontSize, textColor, HudAlign::Center);
}

} // namespace voidfall
