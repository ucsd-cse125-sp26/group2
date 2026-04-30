/// @file Scoreboard.hpp
#pragma once

#include "hud/HudWidget.hpp"

#include <vector>

struct Scoreboard : HudWidget
{
    float panelWidth = 500.f;
    float panelHeight = 400.f;
    float headerFontSize = 18.f;
    float rowFontSize = 14.f;
    float rowHeight = 22.f;

    Scoreboard();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

    /// @brief Set visibility via TAB key (called from Hud::processEvent).
    void setOpen(bool open) { visible = open; }

private:
    std::vector<HudTeamMemberStatus> allies_;
    std::vector<HudTeamMemberStatus> enemies_;
    int allyScore_ = 0, enemyScore_ = 0;
};
