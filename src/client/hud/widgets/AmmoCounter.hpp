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

struct AmmoCounter : HudWidget
{
    float panelWidth = 320.f;
    float panelHeight = 96.f;
    float clipFontSize = 44.f;
    float magFontSize = 18.f;
    float reserveFontSize = 16.f;
    float nameFontSize = 14.f;
    float fireModeFontSize = 9.f;
    float secondaryFontSize = 11.f;

    AmmoCounter();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int displayClip_ = 30;
    int displayReserve_ = 90;
    int displayMag_ = 30; ///< Magazine size (used for "/30" caption).
    int weaponId_ = 0;

    int secondaryClip_ = 0;
    int secondaryReserve_ = 0;
    int secondaryWeaponId_ = -1;
};
