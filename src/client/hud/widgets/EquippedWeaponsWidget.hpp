/// @file EquippedWeaponsWidget.hpp
/// @brief Stacked primary/secondary weapon slot HUD widget.

#pragma once

#include "hud/HudWidget.hpp"

#include <array>

struct EquippedWeaponsWidget : HudWidget
{
    struct SvgPartTuning
    {
        float scale = 1.f;
        float offsetX = 0.f;
        float offsetY = 0.f;
        float stretchX = 1.f;
        float stretchY = 1.f;
    };

    struct WeaponSlotTuning
    {
        SvgPartTuning frame;
        SvgPartTuning icon;
    };

    float frameWidth = 225.f;
    float frameHeight = 90.f;
    float frameGap = 12.5f;
    float weaponIconWidth = 125.f;
    float weaponIconHeight = 42.f;

    std::array<WeaponSlotTuning, 2> slots{};

    EquippedWeaponsWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int primaryWeaponId_ = 0;
    int secondaryWeaponId_ = 2;
    int activeWeaponSlot_ = 0;
};
