/// @file VignetteWidget.cpp
#include "VignetteWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudTween.hpp"

#include <algorithm>

VignetteWidget::VignetteWidget()
{
    anchor = HudAnchor::TopLeft;
    visible = true; // Always "active" — alpha controls visibility.
}

void VignetteWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& tweens)
{
    screenW_ = state.screenW;
    screenH_ = state.screenH;

    // Red vignette: flash on damage, proportional to damage intensity.
    if (state.tookDamage) {
        const float intensity = std::clamp(state.damageIntensity * 3.f, 0.5f, 1.0f);
        damageAlpha_ = intensity;
        tweens.tween(&damageAlpha_, 0.f, 0.6f, easeOutQuad);
    }

    // Blue vignette: flash when armor breaks.
    if (state.armorBroke) {
        shieldAlpha_ = 0.9f;
        tweens.tween(&shieldAlpha_, 0.f, 0.8f, easeOutQuad);
    }

    // Black vignette: fade in on death, reset on respawn.
    if (!state.isAlive && !wasDead_) {
        // Just died — fade to dark.
        deathAlpha_ = 0.f;
        tweens.tween(&deathAlpha_, 0.85f, 1.0f, easeInQuad);
    } else if (state.isAlive && wasDead_) {
        // Just respawned — snap off.
        tweens.cancel(&deathAlpha_);
        deathAlpha_ = 0.f;
    }
    wasDead_ = !state.isAlive;
}

void VignetteWidget::draw(HudContext& ctx, float /*drawX*/, float /*drawY*/)
{
    // Draw order: death (back) → shield → damage (front).
    if (deathAlpha_ > 0.01f)
        ctx.vignette(screenW_, screenH_, HudColor(0.f, 0.f, 0.f, deathAlpha_));

    if (shieldAlpha_ > 0.01f)
        ctx.vignette(screenW_, screenH_, HudColor(0.2f, 0.5f, 1.f, shieldAlpha_));

    if (damageAlpha_ > 0.01f)
        ctx.vignette(screenW_, screenH_, HudColor(1.0f, 0.0f, 0.0f, damageAlpha_));
}
