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
        tweens.tween(&alpha_, 0.f, fadeDuration, easeOutQuad);
        tweens.tween(&scale_, 1.f, 0.15f, easeOutBack);
    }
}

void HitMarkerWidget::draw(HudContext& ctx, float cx, float cy)
{
    if (alpha_ < 0.01f)
        return;

    const HudColor color = isHeadshot_ ? HudColor(1.f, 0.3f, 0.3f, alpha_) : HudColor(1.f, 1.f, 1.f, alpha_);
    const float gap = armGap * scale_;
    const float len = armLength * scale_;
    const float t = armThickness;

    // Four diagonal arms (45° rotated X pattern).
    const float d = 0.707f; // cos(45°)
    const float gd = gap * d;
    const float ld = len * d;

    // Top-right arm
    ctx.rect(cx + gd, cy - gd - ld, t, ld, color);
    // Top-left arm
    ctx.rect(cx - gd - t, cy - gd - ld, t, ld, color);
    // Bottom-right arm
    ctx.rect(cx + gd, cy + gd, t, ld, color);
    // Bottom-left arm
    ctx.rect(cx - gd - t, cy + gd, t, ld, color);
}
