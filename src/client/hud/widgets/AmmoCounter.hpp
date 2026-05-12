/// @file AmmoCounter.hpp
/// @brief Voidfall weapon panel — name + clip/mag/reserve in mil-spec frame.
///
/// Kept under AmmoCounter.* to preserve existing #includes and CMake entries.
/// Renders the bottom-right weapon block from the VOIDFALL prototype:
///
///   ┌─ ARC-9 ─────── AUTO ─┐
///   │   17 / 30   +90      │
///   │ ─── (hairline) ──────│
///   │ [2] PULSAR    7/12   │
///   └──────────────────────┘
///
/// Corner brackets are amber; the live clip count is the hero numeral.

#pragma once

#include "hud/HudWidget.hpp"

/// @brief Apex-style weapon cluster: weapon body up top with a type-color
/// underline + bracketed fire-mode tag, then **two slot tabs side-by-side**
/// at the bottom (one per slot, both visible). The active tab gets the
/// type-color top border and full opacity; the inactive tab is faded.
struct AmmoCounter : HudWidget
{
    float panelWidth = 380.f;
    float panelHeight = 124.f;
    float clipFontSize = 52.f;          ///< Hero numeral.
    float magFontSize = 22.f;           ///< "/<capacity>" caption.
    float reserveFontSize = 20.f;       ///< "+<reserve>" caption.
    float nameFontSize = 18.f;          ///< Weapon name.
    float fireModeFontSize = 11.f;      ///< Fire-mode tag.
    float slotFontSize = 12.f;          ///< Bottom slot-tab text.
    float slotIndexFontSize = 10.f;     ///< Slot-tab index pill ("1"/"2").
    float slotTabHeight = 26.f;         ///< Height of each bottom slot tab.
    float typeUnderlineThickness = 2.f; ///< Underline below weapon body, in type color.

    AmmoCounter();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int displayClip_ = 0;
    int displayReserve_ = 0;
    int displayMag_ = 0; ///< Live magazine capacity from WeaponConfig.magazineSize.
    int weaponId_ = 0;

    int secondaryClip_ = 0;
    int secondaryReserve_ = 0;
    int secondaryMag_ = 0;
    int secondaryWeaponId_ = -1;
    int secondaryKeybind_ = 2; ///< Keybind label for the inactive slot ("1" or "2").
};
