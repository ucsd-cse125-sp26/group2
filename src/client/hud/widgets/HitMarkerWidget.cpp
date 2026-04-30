/// @file HitMarkerWidget.cpp
#include "HitMarkerWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudTween.hpp"

HitMarkerWidget::HitMarkerWidget()
{
    anchor = HudAnchor::Center;
    visible = true; // Always "active" — alpha controls visibility.
}

void HitMarkerWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& tweens)
{
    for (const auto& hc : state.hitConfirms) {
        alpha_ = 1.f;
        scale_ = 1.3f;
        isHeadshot_ = hc.isHeadshot;
        shieldBreak_ = hc.shieldBreak;
        tweens.tween(&alpha_, 0.f, fadeDuration, easeOutQuad);
        tweens.tween(&scale_, 1.f, 0.15f, easeOutBack);
    }
}

void HitMarkerWidget::draw(HudContext& ctx, float cx, float cy)
{
    if (alpha_ < 0.01f)
        return;

    // Color priority: headshot (red) > shield break (blue) > normal (white).
    HudColor color;
    if (isHeadshot_)
        color = HudColor(1.f, 0.3f, 0.3f, alpha_); // red
    else if (shieldBreak_)
        color = HudColor(0.3f, 0.6f, 1.f, alpha_); // blue
    else
        color = HudColor(1.f, 1.f, 1.f, alpha_);   // white
    const float s = uiScale_;
    const float gap = armGap * scale_ * s;
    const float len = armLength * scale_ * s;
    const float t = armThickness * s;

    // Four diagonal arms forming an X pattern.
    // Each arm center is offset from screen center along the 45° diagonal.
    const float d = 0.7071f; // cos(45°) = sin(45°)
    const float armCenterDist = gap + len * 0.5f;

    // Top-right arm (rotated 45°)
    ctx.rotatedRect(cx + d * armCenterDist, cy - d * armCenterDist, t, len, 45.f, color);
    // Top-left arm (rotated -45°)
    ctx.rotatedRect(cx - d * armCenterDist, cy - d * armCenterDist, t, len, -45.f, color);
    // Bottom-right arm (rotated -45°)
    ctx.rotatedRect(cx + d * armCenterDist, cy + d * armCenterDist, t, len, -45.f, color);
    // Bottom-left arm (rotated 45°)
    ctx.rotatedRect(cx - d * armCenterDist, cy + d * armCenterDist, t, len, 45.f, color);
}
