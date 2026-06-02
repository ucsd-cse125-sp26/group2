/// @file EmoteWheelWidget.hpp
/// @brief Radial emote-selection wheel (hold-B), styled like a grenade wheel.

#pragma once

#include "hud/HudWidget.hpp"

/// @brief Center-screen radial menu shown while the emote wheel is held open.
///
/// Lays the emote catalog out around a ring (emote 0 at the top, clockwise),
/// highlighting the sector the player is currently pointing at. The selection
/// is resolved by the input sampler; this widget only visualizes it.
struct EmoteWheelWidget : HudWidget
{
    EmoteWheelWidget();

    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

    // Tunable layout (logical px at 1080p; scaled by uiScale_).
    float radius = 150.f;        ///< Ring radius from center to sector label.
    float sectorRadius = 46.f;   ///< Radius of each emote node.
    float labelFontSize = 15.f;  ///< Emote label text size.
    float titleFontSize = 18.f;  ///< Center "EMOTE" caption size.

private:
    HudEmoteWheelState state_{};
    float openAlpha_ = 0.f; ///< Fade-in/out driver.
};
