/// @file AmmoCounter.hpp
/// @brief Minimal bottom-right ammo readout.

#pragma once

#include "hud/HudWidget.hpp"

/// @brief Shows current magazine ammo and reserve ammo without panel chrome.
struct AmmoCounter : HudWidget
{
    float panelWidth = 230.f;
    float panelHeight = 100.f;
    float clipFontSize = 125.5f;  ///< Current ammo in the magazine.
    float reserveFontSize = 75.f; ///< Reserve / total ammo.
    float edgePadding = 0.f;      ///< Inset from the panel edge to the rightmost number.
    float backgroundScale = 1.26f;
    float backgroundOffsetX = -120.5f;
    float backgroundOffsetY = -4.f;
    float backgroundStretchX = 1.3f;
    float backgroundStretchY = 1.f;

    AmmoCounter();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int displayClip_ = 0;
    int displayReserve_ = 0;
    int weaponId_ = 0;
};
