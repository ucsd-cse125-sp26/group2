/// @file EquipmentSlots.hpp
/// @brief Bottom-center equipment row — grapple / grenade / tactical.
///
/// Three 64×64 mil-spec slots:
///   ┌──┐  ┌──┐  ┌──┐
///   │🪝│  │💣│  │⚡│
///   │ E│  │ G│  │ Q│
///   └──┘  └──┘  └──┘
/// - Each slot's icon goes amber when ready, dimmed when on cooldown.
/// - Cooldown overlay is a black band that drains from top to bottom
///   (covers (1 - charge) × height of the slot).
/// - Slots with a count show the integer; slots without (grapple) show
///   "RDY" or remaining seconds.

#pragma once

#include "hud/HudWidget.hpp"

#include <string>

struct EquipmentSlots : HudWidget
{
    float slotSize = 120.f;
    float slotGap = 50.f;
    float iconSize = 60.f;
    float keyFontSize = 26.f;
    float keyPadX = 10.f;
    float keyPadY = 3.5f;

    EquipmentSlots();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    HudEquipmentState state_;
    std::string primaryAbilityLabel_ = "Left Shift";
    std::string secondaryAbilityLabel_ = "E";
};
