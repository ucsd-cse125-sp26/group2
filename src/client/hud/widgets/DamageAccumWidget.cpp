/// @file DamageAccumWidget.cpp
#include "DamageAccumWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudTween.hpp"

#include <cstdio>

DamageAccumWidget::DamageAccumWidget()
{
    anchor = HudAnchor::Center;
    offsetY = 40.f; // below crosshair
    visible = true;
}

void DamageAccumWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& tweens)
{
    const int newTotal = state.damageAccum.total;

    if (newTotal != displayTotal_) {
        displayTotal_ = newTotal;
        if (newTotal > 0) {
            alpha_ = 1.f;
            tweens.cancel(&alpha_);
        } else {
            // Target switched or timed out — fade out.
            tweens.tween(&alpha_, 0.f, 0.3f, easeOutQuad);
        }
    }
}

void DamageAccumWidget::draw(HudContext& ctx, float cx, float cy)
{
    if (displayTotal_ <= 0 || alpha_ < 0.01f)
        return;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", displayTotal_);

    const float fontSize = 24.f * uiScale_;
    const float outOff = 1.5f * uiScale_;

    // Shadow for readability.
    HudColor shadow(0.f, 0.f, 0.f, 0.7f * alpha_);
    ctx.text(buf, cx + outOff, cy + outOff, fontSize, shadow, HudAlign::Center);

    // White text.
    HudColor color(1.f, 1.f, 1.f, alpha_);
    ctx.text(buf, cx, cy, fontSize, color, HudAlign::Center);
}
