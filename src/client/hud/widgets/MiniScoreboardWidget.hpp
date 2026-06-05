/// @file MiniScoreboardWidget.hpp
/// @brief Compact top-right scoreboard summary.

#pragma once

#include "hud/HudWidget.hpp"

#include <array>
#include <vector>

struct MiniScoreboardWidget : HudWidget
{
    struct RowTuning
    {
        float offsetX = 42.f;
        float offsetY = 48.f;
    };

    float backgroundWidth = 226.f;
    float backgroundHeight = 70.f;
    float backgroundScale = 2.3f;
    float backgroundOffsetX = 0.f;
    float backgroundOffsetY = -39.f;
    float backgroundStretchX = 0.43f;
    float backgroundStretchY = 0.95f;
    float backgroundRotationDeg = 12.f;
    float rowsRotationDeg = -12.5f;
    bool showRowBorders = false;
    float rowFontSize = 30.f;
    float scoreFontSize = 30.f;
    float colorCubeSize = 22.5f;
    float rowBorderPadding = 5.5f;
    float cubeTextGap = 36.f;
    float nameScoreGap = 38.f;
    std::array<RowTuning, 2> rows{{RowTuning{23.f, 38.5f}, RowTuning{23.5f, 85.5f}}};

    MiniScoreboardWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float anchorX, float anchorY) override;

private:
    struct DisplayRow
    {
        HudTeamMemberStatus player;
        bool valid = false;
    };

    std::vector<HudTeamMemberStatus> players_;
    std::array<DisplayRow, 2> displayRows_{};
};
