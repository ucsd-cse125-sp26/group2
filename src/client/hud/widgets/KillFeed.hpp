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
    float panelWidth = 540.f;
    float leaderboardHeight = 142.f;
    float feedHeight = 325.f;
    float feedGap = 20.f;
    float entryHeight = 46.f;
    float entryPadding = 0.f;
    float entryLifetime = 5.f;
    float fontSize = 31.f;
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
        bool permanent = false;
        float timer = 0.f;
        float slideIn = 1.f; ///< 1 = settled, 0 = just-spawned (animates in).
    };
    std::vector<Entry> entries_;
    int allyScore_ = 0;
    int enemyScore_ = 0;
    int localScore_ = 0;
};
