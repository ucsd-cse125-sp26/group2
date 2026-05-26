/// @file PickupPrompt.hpp
/// @brief On-screen "Press <binding> to pick up <Weapon>" hint shown when a
///        weapon pickup is currently actionable for the local player.

#pragma once

#include "hud/HudWidget.hpp"

#include <string>

/// @brief Centered prompt rendered just below the crosshair while the local
///        player is in range of, and looking at, an available weapon spawner.
///
/// Visibility and the displayed weapon name are driven entirely by
/// HudGameState::pickupAvailable / pickupWeaponId — Game.cpp populates them
/// each frame by replicating the WeaponSpawnerSystem proximity check for the
/// local player.
struct PickupPrompt : HudWidget
{
    float fontSize = 32.f;      ///< Size of the descriptive text (logical px).
    float keyFontSize = 32.f;   ///< Size of the key glyph (logical px).
    float keyBoxPadding = 12.f; ///< Padding inside the key glyph box.
    float keyBoxRadius = 4.f;  ///< Corner radius of the key glyph box.
    float spacing = 14.f;      ///< Gap between the key glyph box and the text.

    PickupPrompt();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int weaponId_ = 0;
    std::string keyLabel_ = "F";
};
