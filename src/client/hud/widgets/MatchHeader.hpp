/// @file MatchHeader.hpp
/// @brief Voidfall top-center match readout — large mono time + frag target.
///
/// Replaces the bare "RoundTimer" digit clock with a bracketed mil-spec panel:
///   ┌──────── 07:42 ────────┐
///   │      FRAG · 30        │
///   └───────────────────────┘
/// The big numeric is `MM:SS` driven by `HudGameState::matchInfo.elapsedSeconds`
/// (or `roundTimeRemaining` as a fallback when match metadata is missing).

#pragma once

#include "hud/HudWidget.hpp"

struct MatchHeader : HudWidget
{
    float timeFontSize = 22.f;
    float subFontSize = 9.f;
    float panelPadX = 24.f;
    float panelPadY = 6.f;

    MatchHeader();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int seconds_ = 0;        ///< Current displayed time (seconds).
    int fragTarget_ = 30;
    bool useElapsed_ = true; ///< If false, show countdown instead (round timer fallback).
};
