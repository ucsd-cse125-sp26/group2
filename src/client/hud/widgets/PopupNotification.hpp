/// @file PopupNotification.hpp
/// @brief Bottom-center transient HUD popup messages.

#pragma once

#include "hud/HudWidget.hpp"

#include <string>
#include <vector>

struct PopupNotification : HudWidget
{
    float entryHeight = 30.f;
    float entryGap = 6.f;
    float entryFontSize = 13.f;
    float entryLifetime = 4.f;
    float fadeOut = 0.65f;
    int maxEntries = 3;

    PopupNotification();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    struct Entry
    {
        HudPopupKind kind = HudPopupKind::Info;
        std::string text;
        float timer = 0.f;
        float slideIn = 0.f;
    };

    std::vector<Entry> entries_;
};
