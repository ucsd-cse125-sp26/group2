/// @file PickupNotification.hpp
/// @brief Right-side slide-in messages for items just picked up.
///
/// Distinct from `PickupPrompt` (which shows the configured pickup binding).
/// This widget surfaces the *result* of pickups — "+30 PULSE·MAG", "+1 SHIELD·CELL"
/// — as transient slide-in pills along the right edge.

#pragma once

#include "hud/HudWidget.hpp"

#include <string>
#include <vector>

struct PickupNotification : HudWidget
{
    float entryHeight = 22.f;
    float entryGap = 4.f;
    float entryFontSize = 11.f;
    float entryLifetime = 4.f;
    float fadeOut = 0.6f;

    PickupNotification();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    struct Entry
    {
        std::string label;
        int qty = 1;
        float timer = 0.f;
        float slideIn = 0.f;
    };
    std::vector<Entry> entries_;
};
