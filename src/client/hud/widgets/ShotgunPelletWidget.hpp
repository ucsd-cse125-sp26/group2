/// @file ShotgunPelletWidget.hpp
/// @brief Per-shot 9-pellet readout for the shotgun.
///
/// Renders the 9 pellets of the most recent shotgun blast as dots in an
/// asterisk pattern (1 centre + 8 outer in N/NE/E/SE/S/SW/W/NW order, matching
/// the server's `k_offsets` in WeaponSystem.cpp). Per-pellet colour:
///   - white = pellet missed
///   - red   = pellet hit a body
///   - gold  = pellet hit a head
/// Widget fades in on shot, holds briefly, then fades out.

#pragma once

#include "hud/HudWidget.hpp"

struct ShotgunPelletWidget : HudWidget
{
    float radius = 18.f;           ///< Pixel radius of the outer ring at uiScale=1.
    float dotRadius = 3.5f;        ///< Pixel radius of each pellet dot at uiScale=1.
    float holdDuration = 0.45f;    ///< Full opacity hold time after a blast.
    float fadeDuration = 0.55f;    ///< Linear fade-out duration after the hold.

    ShotgunPelletWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    HudShotgunBlast latest_{}; ///< Cached copy of the last blast we displayed.
    float alpha_ = 0.f;        ///< Current widget opacity.
    float age_ = 0.f;          ///< Seconds since the latest blast was staged.
    bool primed_ = false;      ///< False until the first blast arrives.
};
