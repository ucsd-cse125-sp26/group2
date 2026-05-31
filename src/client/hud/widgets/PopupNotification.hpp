/// @file PopupNotification.hpp
/// @brief Bottom-center transient HUD popup messages.

#pragma once

#include "hud/HudWidget.hpp"

#include <string>
#include <vector>

struct PopupNotification : HudWidget
{
    float entryHeight = 30.f;   ///< Height of each popup pill in 1080p-scaled HUD units.
    float entryGap = 6.f;       ///< Vertical spacing between stacked popup pills.
    float entryFontSize = 13.f; ///< Popup text size in 1080p-scaled HUD units.
    float entryLifetime = 4.f;  ///< Seconds each popup remains alive.
    float fadeOut = 0.65f;      ///< Seconds used for the final fade-out.
    int maxEntries = 3;         ///< Maximum simultaneous popup pills retained.

    /// @brief Construct the popup feed anchored above the bottom-center ability bar.
    PopupNotification();

    /// @brief Consume this-frame popup messages and advance retained popup lifetimes.
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;

    /// @brief Draw active popup pills centered around the resolved HUD anchor.
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    struct Entry
    {
        HudPopupKind kind = HudPopupKind::Info; ///< Styling category copied from the HUD event.
        std::string text;                       ///< Display text copied from the HUD event.
        float timer = 0.f;                      ///< Remaining visible lifetime in seconds.
        float slideIn = 0.f;                    ///< 0..1 slide/fade-in interpolation value.
    };

    std::vector<Entry> entries_;
};
