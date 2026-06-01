/// @file Scoreboard.hpp
#pragma once

#include "hud/HudWidget.hpp"

#include <vector>

struct Scoreboard : HudWidget
{
    float panelWidth = 600.f;
    float panelHeight = 450.f;
    float headerFontSize = 22.f;
    float rowFontSize = 18.f;
    float rowHeight = 26.f;

    Scoreboard();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

    /// @brief Set visibility via TAB key (called from Hud::processEvent).
    void setOpen(bool open) { manualOpen_ = open; }

private:
    std::vector<HudTeamMemberStatus> allies_;
    std::vector<HudTeamMemberStatus> enemies_;
    int allyScore_ = 0, enemyScore_ = 0;
    bool manualOpen_ = false;
};
