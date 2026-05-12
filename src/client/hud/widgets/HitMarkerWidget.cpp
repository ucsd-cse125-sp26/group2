/// @file HitMarkerWidget.cpp
/// @brief Voidfall hit-marker with per-type shape.

#include "HitMarkerWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudTween.hpp"
#include "hud/VoidfallStyle.hpp"

#include <cmath>
#include <numbers>

HitMarkerWidget::HitMarkerWidget()
{
    anchor = HudAnchor::Center;
    visible = true;
}

void HitMarkerWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& tweens)
{
    for (const auto& hc : state.hitConfirms) {
        if (hc.isKill)
            kind_ = Kind::Kill;
        else if (hc.isHeadshot)
            kind_ = Kind::Headshot;
        else if (hc.shieldBreak)
            kind_ = Kind::Shield;
        else
            kind_ = Kind::Hp;

        alpha_ = 1.f;
        scale_ = 1.3f;
        const float dur = (kind_ == Kind::Kill) ? killFadeDuration : fadeDuration;
        tweens.tween(&alpha_, 0.f, dur, easeOutQuad);
        tweens.tween(&scale_, 1.f, 0.15f, easeOutBack);
    }
}

void HitMarkerWidget::draw(HudContext& ctx, float cx, float cy)
{
    using namespace voidfall;

    if (alpha_ < 0.01f || kind_ == Kind::None)
        return;

    const float s = uiScale_;
    const float gap = armGap * scale_ * s;
    const float len = armLength * scale_ * s;

    const float d = 0.7071f; // cos/sin(45°).
    const float armCenterDist = gap + len * 0.5f;

    auto withFade = [&](HudColor c) -> HudColor { return HudColor{c.r, c.g, c.b, c.a * alpha_}; };

    if (kind_ == Kind::Shield) {
        const float t = (armThickness - 0.4f) * s;
        const HudColor col = withFade(k_cyan);
        ctx.rotatedRect(cx + d * armCenterDist, cy - d * armCenterDist, t, len, 45.f, col);
        ctx.rotatedRect(cx - d * armCenterDist, cy - d * armCenterDist, t, len, -45.f, col);
        ctx.rotatedRect(cx + d * armCenterDist, cy + d * armCenterDist, t, len, -45.f, col);
        ctx.rotatedRect(cx - d * armCenterDist, cy + d * armCenterDist, t, len, 45.f, col);
    } else if (kind_ == Kind::Hp) {
        const float t = armThickness * s;
        const HudColor col = withFade(k_amber);
        ctx.rotatedRect(cx + d * armCenterDist, cy - d * armCenterDist, t, len, 45.f, col);
        ctx.rotatedRect(cx - d * armCenterDist, cy - d * armCenterDist, t, len, -45.f, col);
        ctx.rotatedRect(cx + d * armCenterDist, cy + d * armCenterDist, t, len, -45.f, col);
        ctx.rotatedRect(cx - d * armCenterDist, cy + d * armCenterDist, t, len, 45.f, col);
    } else if (kind_ == Kind::Headshot) {
        // Four small filled triangles pointing in toward the center at the
        // cardinals.  We approximate triangles with a small thick rect rotated
        // 45° (looks like a chevron point at this size).
        const float tri = headshotTriangleSize * scale_ * s;
        const HudColor col = withFade(k_red);
        // Top.
        ctx.rotatedRect(cx, cy - gap - tri * 0.5f, tri, tri, 45.f, col);
        // Bottom.
        ctx.rotatedRect(cx, cy + gap + tri * 0.5f, tri, tri, 45.f, col);
        // Left.
        ctx.rotatedRect(cx - gap - tri * 0.5f, cy, tri, tri, 45.f, col);
        // Right.
        ctx.rotatedRect(cx + gap + tri * 0.5f, cy, tri, tri, 45.f, col);
    } else if (kind_ == Kind::Kill) {
        // Bright white outer ring + thick diagonal X.
        const float r = killRingRadius * scale_ * s;
        const HudColor col = withFade(k_textBright);
        // Ring approximated with small rotated rectangles around the perimeter.
        const float ringT = 1.0f * s;
        const int segments = 32;
        for (int i = 0; i < segments; ++i) {
            const float a0 = (static_cast<float>(i) / segments) * (2.f * std::numbers::pi_v<float>);
            const float bx = cx + std::cos(a0) * r;
            const float by = cy + std::sin(a0) * r;
            const float segLen = ((2.f * std::numbers::pi_v<float>)*r) / segments + 0.6f * s;
            const float ang = (a0 * 180.f / std::numbers::pi_v<float>)+90.f;
            ctx.rotatedRect(bx, by, ringT, segLen, ang, col);
        }
        // Thicker diagonal X over the ring.
        const float t = (armThickness + 0.4f) * s;
        const float gap2 = (armGap + 1.f) * scale_ * s;
        const float len2 = (armLength + 1.f) * scale_ * s;
        const float armCenter2 = gap2 + len2 * 0.5f;
        ctx.rotatedRect(cx + d * armCenter2, cy - d * armCenter2, t, len2, 45.f, col);
        ctx.rotatedRect(cx - d * armCenter2, cy - d * armCenter2, t, len2, -45.f, col);
        ctx.rotatedRect(cx + d * armCenter2, cy + d * armCenter2, t, len2, -45.f, col);
        ctx.rotatedRect(cx - d * armCenter2, cy + d * armCenter2, t, len2, 45.f, col);
    }
}
