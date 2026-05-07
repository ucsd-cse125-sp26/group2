/// @file CompassWidget.hpp
/// @brief Top-center heading strip — cardinals, ticks, current bearing readout.
///
/// Mirrors the prototype's `CompassStrip`:
///   - 360 px wide thin panel below the match header.
///   - Center pointer (amber chevron).
///   - Tick marks every 5°; bigger ticks at 45°; tallest at cardinals.
///   - Cardinal labels (N/NE/E/SE/...) above each major tick.
///   - Bearing readout ("142°") below the strip.
///
/// Driven by `HudGameState::localPlayerYaw` (radians, 0 = +Z, CW).

#pragma once

#include "hud/HudWidget.hpp"

struct CompassWidget : HudWidget
{
    float stripWidth = 360.f;
    float stripHeight = 22.f;
    float fovDeg = 120.f; ///< Total angular coverage shown across the strip.
    float labelFontSize = 9.f;
    float bearingFontSize = 9.f;

    CompassWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    float headingDeg_ = 0.f; ///< Current heading (degrees, 0 = north / +Z, CW).
};
