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

/// @brief Deep panel background tint used by translucent HUD glass.
constexpr HudColor k_primary{0.168627f, 0.694118f, 0.741176f, 1.0f};    // #2BB1BD
constexpr HudColor k_secondary{0.250980f, 0.478431f, 0.501961f, 1.0f};  // #407A80
constexpr HudColor k_tertiary{0.615686f, 0.858824f, 0.882353f, 1.0f};   // #9DDBE1
constexpr HudColor k_quaternary{0.070588f, 0.270588f, 0.290196f, 1.0f}; // #12454A

constexpr HudColor k_bgVoid{k_quaternary.r, k_quaternary.g, k_quaternary.b, 0.96f};
/// @brief Standard panel fill — translucent glass base, reinforced by glow
/// layers in drawPanel() for readability without a flat sticker look.
constexpr HudColor k_bgPanel{k_quaternary.r, k_quaternary.g, k_quaternary.b, 0.56f};
/// @brief Solid panel — used for hero callouts.
constexpr HudColor k_bgPanelSolid{k_quaternary.r, k_quaternary.g, k_quaternary.b, 0.78f};
/// @brief Inset bar background.
constexpr HudColor k_bgInset{k_quaternary.r, k_quaternary.g, k_quaternary.b, 0.58f};

/// @brief Dim hairline — `oklch(0.32 0.015 60 / 0.6)`.
constexpr HudColor k_lineDim{k_secondary.r, k_secondary.g, k_secondary.b, 0.7f};
/// @brief Standard hairline — `oklch(0.40 0.018 60)`.
constexpr HudColor k_line{k_secondary.r, k_secondary.g, k_secondary.b, 0.95f};
/// @brief Bright hairline — `oklch(0.55 0.02 60)`.
constexpr HudColor k_lineBright{k_tertiary.r, k_tertiary.g, k_tertiary.b, 1.0f};

/// @brief Dim text — bumped luminance from ~0.55 to ~0.78 so secondary
/// readouts (fire mode, "+reserve", "/mag", "[2] PULSAR") read clearly
/// against the panel chrome instead of fading into it.
constexpr HudColor k_textDim{k_secondary.r, k_secondary.g, k_secondary.b, 1.0f};
/// @brief Standard text — bumped slightly toward white for body chrome.
constexpr HudColor k_text{k_tertiary.r, k_tertiary.g, k_tertiary.b, 1.0f};
/// @brief Brightest text — kept near-white for hero readouts.
constexpr HudColor k_textBright{k_tertiary.r, k_tertiary.g, k_tertiary.b, 1.0f};

/// @brief Primary amber — `oklch(0.80 0.165 75)`.
constexpr HudColor k_amber{k_primary.r, k_primary.g, k_primary.b, 1.0f};
/// @brief Dimmer amber — `oklch(0.65 0.13 75)`.
constexpr HudColor k_amberDim{k_secondary.r, k_secondary.g, k_secondary.b, 1.0f};
/// @brief Deep amber — `oklch(0.45 0.10 70)`.
constexpr HudColor k_amberDeep{k_quaternary.r, k_quaternary.g, k_quaternary.b, 1.0f};
/// @brief Amber glow tint — `oklch(0.80 0.165 75 / 0.15)`.
constexpr HudColor k_amberGlow{k_primary.r, k_primary.g, k_primary.b, 0.18f};
constexpr HudColor k_glassHighlight{k_tertiary.r, k_tertiary.g, k_tertiary.b, 0.18f};
constexpr HudColor k_glassShadow{k_quaternary.r, k_quaternary.g, k_quaternary.b, 0.28f};
constexpr HudColor k_scanline{k_tertiary.r, k_tertiary.g, k_tertiary.b, 0.045f};
constexpr HudColor k_chromaRed{1.0f, 0.18f, 0.10f, 0.16f};
constexpr HudColor k_chromaCyan{0.16f, 0.90f, 1.0f, 0.16f};

/// @brief Health red — `oklch(0.65 0.20 28)`.
constexpr HudColor k_red{k_primary.r, k_primary.g, k_primary.b, 1.0f};
/// @brief Bright tail of HP gradient — `oklch(0.72 0.18 35)`.
constexpr HudColor k_redBright{k_tertiary.r, k_tertiary.g, k_tertiary.b, 1.0f};
/// @brief Dim red — `oklch(0.50 0.15 28)`.
constexpr HudColor k_redDim{k_secondary.r, k_secondary.g, k_secondary.b, 1.0f};
constexpr HudColor k_health{0.92f, 0.96f, 0.97f, 1.0f};
constexpr HudColor k_healthBright{1.0f, 1.0f, 1.0f, 1.0f};

/// @brief Shield cyan — `oklch(0.80 0.10 220)`.
constexpr HudColor k_cyan{k_tertiary.r, k_tertiary.g, k_tertiary.b, 1.0f};
/// @brief Dim cyan — `oklch(0.55 0.08 220)`.
constexpr HudColor k_cyanDim{k_secondary.r, k_secondary.g, k_secondary.b, 1.0f};

/// @brief Status green — `oklch(0.78 0.16 145)`.
constexpr HudColor k_green{k_tertiary.r, k_tertiary.g, k_tertiary.b, 1.0f};

/// @brief Pickup amber-trim background.
constexpr HudColor k_pickupBg{k_quaternary.r, k_quaternary.g, k_quaternary.b, 0.82f};

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
