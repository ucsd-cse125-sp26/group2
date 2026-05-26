/// @file VignetteWidget.cpp
#include "VignetteWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudTween.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>

using namespace voidfall;

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
    // Persistent visor artifacts: subtle enough to read as optical projection,
    // not a damage state.
    ctx.rect(0.f, 0.f, 1.f, screenH_, withAlpha(k_chromaRed, 0.18f));
    ctx.rect(screenW_ - 1.f, 0.f, 1.f, screenH_, withAlpha(k_chromaCyan, 0.18f));
    ctx.rect(0.f, 0.f, screenW_, 1.f, withAlpha(k_chromaCyan, 0.10f));
    ctx.rect(0.f, screenH_ - 1.f, screenW_, 1.f, withAlpha(k_chromaRed, 0.10f));

    // Draw order: death (back) → shield → damage (front).
    if (deathAlpha_ > 0.01f)
        ctx.vignette(screenW_, screenH_, withAlpha(k_quaternary, deathAlpha_));

    if (shieldAlpha_ > 0.01f)
        ctx.vignette(screenW_, screenH_, withAlpha(k_tertiary, shieldAlpha_));

    if (damageAlpha_ > 0.01f)
        ctx.vignette(screenW_, screenH_, withAlpha(k_primary, damageAlpha_));
}
