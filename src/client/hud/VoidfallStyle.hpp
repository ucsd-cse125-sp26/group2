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

namespace voidfall
{

// ── Palette (matches prototype's CSS custom properties) ─────────────────────

/// @brief Deep panel background — opaque so HUD chrome doesn't pick up sky tint.
constexpr HudColor k_bgVoid{0.08f, 0.075f, 0.07f, 0.96f};
/// @brief Standard panel fill — bumped to 0.92α (was 0.78).
/// At 0.78 the world bled through enough that bright-sky maps washed the
/// chrome out and made dim text grey-on-grey.  0.92 keeps a faint sense of
/// translucency without sacrificing readability against any background.
constexpr HudColor k_bgPanel{0.10f, 0.095f, 0.085f, 0.92f};
/// @brief Solid panel — used for hero callouts.
constexpr HudColor k_bgPanelSolid{0.09f, 0.085f, 0.075f, 0.96f};
/// @brief Inset bar background.
constexpr HudColor k_bgInset{0.12f, 0.115f, 0.105f, 0.92f};

/// @brief Dim hairline — `oklch(0.32 0.015 60 / 0.6)`.
constexpr HudColor k_lineDim{0.32f, 0.30f, 0.28f, 0.7f};
/// @brief Standard hairline — `oklch(0.40 0.018 60)`.
constexpr HudColor k_line{0.42f, 0.40f, 0.37f, 0.95f};
/// @brief Bright hairline — `oklch(0.55 0.02 60)`.
constexpr HudColor k_lineBright{0.62f, 0.59f, 0.56f, 1.0f};

/// @brief Dim text — bumped luminance from ~0.55 to ~0.78 so secondary
/// readouts (fire mode, "+reserve", "/mag", "[2] PULSAR") read clearly
/// against the panel chrome instead of fading into it.
constexpr HudColor k_textDim{0.78f, 0.76f, 0.73f, 1.0f};
/// @brief Standard text — bumped slightly toward white for body chrome.
constexpr HudColor k_text{0.92f, 0.90f, 0.87f, 1.0f};
/// @brief Brightest text — kept near-white for hero readouts.
constexpr HudColor k_textBright{1.00f, 0.99f, 0.97f, 1.0f};

/// @brief Primary amber — `oklch(0.80 0.165 75)`.
constexpr HudColor k_amber{1.00f, 0.71f, 0.18f, 1.0f};
/// @brief Dimmer amber — `oklch(0.65 0.13 75)`.
constexpr HudColor k_amberDim{0.78f, 0.55f, 0.14f, 1.0f};
/// @brief Deep amber — `oklch(0.45 0.10 70)`.
constexpr HudColor k_amberDeep{0.50f, 0.35f, 0.10f, 1.0f};
/// @brief Amber glow tint — `oklch(0.80 0.165 75 / 0.15)`.
constexpr HudColor k_amberGlow{1.00f, 0.71f, 0.18f, 0.18f};

/// @brief Health red — `oklch(0.65 0.20 28)`.
constexpr HudColor k_red{0.92f, 0.30f, 0.20f, 1.0f};
/// @brief Bright tail of HP gradient — `oklch(0.72 0.18 35)`.
constexpr HudColor k_redBright{0.95f, 0.45f, 0.25f, 1.0f};
/// @brief Dim red — `oklch(0.50 0.15 28)`.
constexpr HudColor k_redDim{0.65f, 0.20f, 0.15f, 1.0f};

/// @brief Shield cyan — `oklch(0.80 0.10 220)`.
constexpr HudColor k_cyan{0.45f, 0.78f, 0.96f, 1.0f};
/// @brief Dim cyan — `oklch(0.55 0.08 220)`.
constexpr HudColor k_cyanDim{0.20f, 0.50f, 0.70f, 1.0f};

/// @brief Status green — `oklch(0.78 0.16 145)`.
constexpr HudColor k_green{0.30f, 0.86f, 0.45f, 1.0f};

/// @brief Pickup amber-trim background.
constexpr HudColor k_pickupBg{0.13f, 0.12f, 0.11f, 0.82f};

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

/// @brief Draw a Voidfall panel: solid fill + 1 px outline.
inline void drawPanel(HudContext& ctx,
                      float x,
                      float y,
                      float w,
                      float h,
                      HudColor fill = k_bgPanel,
                      HudColor border = k_line,
                      float thickness = 1.f)
{
    ctx.rect(x, y, w, h, fill);
    ctx.rectOutline(x, y, w, h, thickness, border);
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
                         HudColor trailColor = HudColor{0.95f, 0.95f, 0.95f, 0.45f},
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
                                 HudColor trailColor = HudColor{0.95f, 0.95f, 0.95f, 0.45f},
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
                       HudColor bgColor = HudColor{0.f, 0.f, 0.f, 0.45f},
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
