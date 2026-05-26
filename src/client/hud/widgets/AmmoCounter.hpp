/// @file AmmoCounter.hpp
/// @brief Bottom-right glass ammo readout.

#pragma once

#include "hud/HudWidget.hpp"

/// @brief Shows current magazine ammo and reserve ammo in prototype HUD chrome.
struct AmmoCounter : HudWidget
{
    float panelWidth = 260.f;
    float panelHeight = 116.f;
    float clipFontSize = 92.f;    ///< Current ammo in the magazine.
    float reserveFontSize = 47.f; ///< Reserve / total ammo.
    float edgePadding = 22.f;     ///< Inset from the panel edge to the rightmost number.

    AmmoCounter();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int displayClip_ = 0;
    int displayReserve_ = 0;
    int weaponId_ = 0;
};
