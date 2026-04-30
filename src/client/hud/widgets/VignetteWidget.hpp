/// @file VignetteWidget.hpp
/// @brief Full-screen vignette overlays for damage, shield break, and death.

#pragma once

#include "hud/HudWidget.hpp"

/// @brief Manages three layered vignette effects:
///   - Red flash when the local player takes damage.
///   - Blue flash when the player's armor/shield is depleted.
///   - Black fade-in when the player is dead.
struct VignetteWidget : HudWidget
{
    VignetteWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    float damageAlpha_ = 0.f; ///< Red vignette intensity (fades out).
    float shieldAlpha_ = 0.f; ///< Blue vignette intensity (fades out).
    float deathAlpha_ = 0.f;  ///< Black vignette intensity (fades in on death).
    bool wasDead_ = false;    ///< Tracks death state transitions.
    float screenW_ = 1280.f;
    float screenH_ = 720.f;
};
