/// @file AmmoCounter.hpp
/// @brief Bottom-right glass ammo readout.

#pragma once

#include "hud/HudWidget.hpp"

/// @brief Shows current magazine ammo and reserve ammo in prototype HUD chrome.
struct AmmoCounter : HudWidget
{
    float panelWidth = 440.f;
    float panelHeight = 150.f;
    float weaponSlotWidth = 440.f;
    float weaponSlotHeight = 140.f;
    float weaponSlotGap = 18.f;
    float weaponSlotBottomGap = 20.f;
    float clipFontSize = 118.f;   ///< Current ammo in the magazine.
    float reserveFontSize = 70.f; ///< Reserve / total ammo.
    float edgePadding = 35.f;     ///< Inset from the panel edge to the rightmost number.

    AmmoCounter();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int displayClip_ = 0;
    int displayReserve_ = 0;
    int weaponId_ = 0;
    int secondaryKeybind_ = 2;
};
