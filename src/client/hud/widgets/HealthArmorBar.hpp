/// @file HealthArmorBar.hpp
/// @brief Voidfall vitals panel — shield over HP, ghost-trail damage drain.
///
/// Renamed-in-spirit to "Vitals" but kept under HealthArmorBar.* to preserve
/// the existing #includes throughout the codebase.
///
/// Layout follows the VOIDFALL design:
///   [shield-icon]  ▓▓▓▓░░░░  45     ← thin cyan bar on top
///   [hp-icon]      ████████▒▒  78   ← thicker red bar with bigger numeral
///
/// Both bars carry a translucent "ghost trail" that lingers from the previous
/// value when damage lands, so a player sees both the post-hit value and what
/// it just dropped from for ~half a second.

#pragma once

#include "hud/HudWidget.hpp"

struct HealthArmorBar : HudWidget
{
    // Layout constants — Apex-style chamfered plate.
    float panelWidth = 340.f; ///< Plate width (matches design v2).
    float panelPadX = 16.f;   ///< Inner horizontal padding.
    float panelPadY = 10.f;   ///< Inner vertical padding.
    float chamferSize = 14.f; ///< Bottom-right cut-corner depth (px).
    float shieldBarHeight = 6.f;
    float healthBarHeight = 14.f;
    float lvlBarHeight = 6.f;
    float barSpacing = 6.f;       ///< Vertical gap between shield and HP rows.
    int shieldSegments = 3;       ///< Apex-style segmented shield bar.
    float shieldSegmentGap = 3.f; ///< Gap between adjacent shield segments.
    float iconSize = 14.f;
    float iconGap = 10.f;
    float trailHoldSeconds = 0.4f;  ///< How long the ghost trail lingers.
    float trailDrainSeconds = 0.6f; ///< How long the drain animation takes after holding.

    HealthArmorBar();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int displayHealth_ = 100;
    int displayArmor_ = 0;
    int maxHealth_ = 100;
    int maxArmor_ = 100;

    // Ghost-trail values (slowly drain toward live values after damage).
    float trailHealth_ = 1.f;
    float trailArmor_ = 0.f;
    float trailHpHoldTimer_ = 0.f;
    float trailShHoldTimer_ = 0.f;

    // Live fills (snap immediately to the current values).
    float liveHealth_ = 1.f;
    float liveArmor_ = 0.f;
    float liveLevel_ = 0.f;
    float trailLevel_ = 0.f;
};
