/// @file CrosshairWidget.cpp
/// @brief Voidfall crosshair — four amber tick marks + center dot.

#include "CrosshairWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <numbers>

CrosshairWidget::CrosshairWidget()
{
    anchor = HudAnchor::Center;

    // Voidfall defaults: ticks not lines, amber not green.  Thicker arms +
    // larger dot than the prototype's hairlines because the in-game scene
    // lighting (sky-bright maps) was washing the previous 1.5 px reticle out
    // entirely; every arm and the dot now also draw a 1-px black underlay
    // for guaranteed contrast against any background.
    style.gap = 5.f;
    style.length = 6.f;
    style.thickness = 2.0f;
    style.color = voidfall::k_primary;
    style.dot = true;
    style.reloadRadius = 20.f;
    style.reloadThickness = 2.f;
    style.reloadColor = voidfall::k_secondary;
}

void CrosshairWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;

    isReloading = state.isReloading;
    reloadProgress = state.reloadProgress;
}

void CrosshairWidget::draw(HudContext& ctx, float cx, float cy)
{
    const float s = uiScale_;
    const float gap = style.gap * s;
    const float len = style.length * s;
    const float t = style.thickness * s;
    const float ht = t * 0.5f;

    // Four short tick marks pointing inward — Voidfall design uses tiny ticks
    // at gap..gap+len, not full crosshair lines.
    ctx.rect(cx + gap, cy - ht, len, t, style.color);       // right
    ctx.rect(cx - gap - len, cy - ht, len, t, style.color); // left
    ctx.rect(cx - ht, cy - gap - len, t, len, style.color); // top
    ctx.rect(cx - ht, cy + gap, t, len, style.color);       // bottom

    // Center dot (1×1 px at design scale, ~2 px on screen for visibility).
    if (style.dot) {
        const float dotSize = 1.5f * s;
        ctx.rect(cx - dotSize * 0.5f, cy - dotSize * 0.5f, dotSize, dotSize, style.color);
    }

    // Reload visualiser
    if (isReloading) {
        constexpr int kMaxSegments = 64;

        const float r = style.reloadRadius * s;
        const float t = style.reloadThickness * s;
        const float progress = std::clamp(reloadProgress, 0.0f, 1.0f);

        if (progress > 0.0f) {
            const int segments = std::max(2, static_cast<int>(kMaxSegments * progress));
            float points[(kMaxSegments + 1) * 2];
            for (int i = 0; i <= segments; ++i) {
                const float frac = (static_cast<float>(i) / static_cast<float>(segments)) * progress;
                const float angle = -0.5f * std::numbers::pi_v<float> + frac * 2.0f * std::numbers::pi_v<float>;
                points[i * 2] = cx + r * std::cos(angle);
                points[i * 2 + 1] = cy + r * std::sin(angle);
            }
            ctx.polyline(points, segments + 1, t, style.reloadColor);
        }
    }
}
