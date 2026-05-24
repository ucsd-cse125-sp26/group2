/// @file KillFeed.hpp
/// @brief Voidfall sliding kill feed (top-right).
///
/// Each entry is a thin bracketed pill:
///   ┌──────────────────────────┐
///   │ VYRE-07  ARC-9  ✕  RAIDEN│
///   └──────────────────────────┘
/// - Killer name in white (amber-tinted if local player).
/// - Weapon abbreviation in amber.
/// - Headshot glyph (small red circle-with-tick).
/// - Victim name in red (or local-player red highlight when victim is "You").
///
/// Entries fade out over the last `fadeOutDuration` seconds of their lifetime.

#pragma once

#include "hud/HudWidget.hpp"

#include <string>
#include <vector>

struct KillFeed : HudWidget
{
    float entryHeight = 22.f;
    float entryPadding = 6.f;
    float entryLifetime = 5.f;
    float fontSize = 11.f;
    float fadeOutDuration = 0.6f;
    int maxEntries = 6;

    KillFeed();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    struct Entry
    {
        std::string killerName;
        std::string victimName;
        bool isHeadshot = false;
        bool youAreKiller = false;
        bool youAreVictim = false;
        float timer = 0.f;
        float slideIn = 1.f; ///< 1 = settled, 0 = just-spawned (animates in).
    };
    std::vector<Entry> entries_;
};
