/// @file KillFeed.hpp
/// @brief Sliding kill feed entries anchored top-right.

#pragma once

#include "hud/HudWidget.hpp"

#include <string>
#include <vector>

struct KillFeed : HudWidget
{
    float entryHeight = 22.f;
    float entryPadding = 4.f;
    float entryLifetime = 5.f;
    float fontSize = 14.f;
    float fadeOutDuration = 0.5f;
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
        float timer = 0.f; ///< Remaining display time.
    };
    std::vector<Entry> entries_;
};
