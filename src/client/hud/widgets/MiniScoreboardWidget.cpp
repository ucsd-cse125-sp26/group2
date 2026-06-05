/// @file MiniScoreboardWidget.cpp
/// @brief Compact top-right scoreboard summary.

#include "MiniScoreboardWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudIcons.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace
{
bool isBetterScoreRow(const HudTeamMemberStatus& lhs, const HudTeamMemberStatus& rhs)
{
    if (lhs.kills != rhs.kills)
        return lhs.kills > rhs.kills;
    if (lhs.deaths != rhs.deaths)
        return lhs.deaths < rhs.deaths;
    if (lhs.isLocal != rhs.isLocal)
        return lhs.isLocal;
    return lhs.name < rhs.name;
}
} // namespace

MiniScoreboardWidget::MiniScoreboardWidget()
{
    anchor = HudAnchor::TopRight;
    offsetX = -256.f;
    offsetY = 67.f;
    width = 228.825f;
    height = 157.5f;
}

void MiniScoreboardWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    players_.assign(state.allies.begin(), state.allies.end());
    displayRows_ = {};
    visible = state.isAlive && !players_.empty();

    if (players_.empty())
        return;

    int bestIndex = 0;
    int localIndex = -1;
    for (int i = 0; i < static_cast<int>(players_.size()); ++i) {
        if (players_[static_cast<std::size_t>(i)].isLocal)
            localIndex = i;
        if (isBetterScoreRow(players_[static_cast<std::size_t>(i)], players_[static_cast<std::size_t>(bestIndex)]))
            bestIndex = i;
    }

    displayRows_[0].player = players_[static_cast<std::size_t>(bestIndex)];
    displayRows_[0].valid = true;

    int secondIndex = -1;
    if (localIndex >= 0 && localIndex != bestIndex) {
        secondIndex = localIndex;
    } else {
        for (int i = 0; i < static_cast<int>(players_.size()); ++i) {
            if (i == bestIndex)
                continue;
            if (secondIndex < 0 ||
                isBetterScoreRow(players_[static_cast<std::size_t>(i)], players_[static_cast<std::size_t>(secondIndex)])) {
                secondIndex = i;
            }
        }
    }

    if (secondIndex >= 0) {
        displayRows_[1].player = players_[static_cast<std::size_t>(secondIndex)];
        displayRows_[1].valid = true;
    }
}

void MiniScoreboardWidget::draw(HudContext& ctx, float anchorX, float anchorY)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float safeBgScale = std::max(0.01f, backgroundScale);
    const float safeBgStretchX = std::max(0.01f, backgroundStretchX);
    const float safeBgStretchY = std::max(0.01f, backgroundStretchY);
    const float bgW = backgroundWidth * s * safeBgScale * safeBgStretchX;
    const float bgH = backgroundHeight * s * safeBgScale * safeBgStretchY;
    const float bgX = anchorX - bgW * 0.5f + backgroundOffsetX * s;
    const float bgY = anchorY + backgroundOffsetY * s;
    const float bgCx = bgX + bgW * 0.5f;
    const float bgCy = bgY + bgH * 0.5f;

    const std::size_t backgroundStart = ctx.vertices().size();
    ctx.svg(HudIcon::Scoreboard, bgX, bgY, bgW, bgH);
    ctx.rotateVertices(backgroundStart, bgCx, bgCy, backgroundRotationDeg);

    const float fs = rowFontSize * s;
    const float scoreFs = scoreFontSize * s;
    const float cube = colorCubeSize * s;
    const float borderPad = rowBorderPadding * s;
    const float cubeGap = cubeTextGap * s;
    const float scoreGap = nameScoreGap * s;
    const std::size_t rowsStart = ctx.vertices().size();

    for (std::size_t i = 0; i < displayRows_.size(); ++i) {
        if (!displayRows_[i].valid)
            continue;

        const auto& row = displayRows_[i];
        const float rowX = bgX + rows[i].offsetX * s;
        const HudColor textColor = row.player.isAlive ? (row.player.isLocal ? k_textBright : k_text) : withAlpha(k_textDim, 0.58f);
        const HudColor scoreColor = row.player.isAlive ? k_textBright : withAlpha(k_textDim, 0.58f);
        HudColor playerColor = row.player.color;
        if (!row.player.isAlive)
            playerColor = withAlpha(playerColor, 0.48f);

        char score[16];
        SDL_snprintf(score, sizeof(score), "%d", row.player.kills);
        const float nameWidth = ctx.measureText(row.player.name.c_str(), fs);
        const float scoreWidth = ctx.measureText(score, scoreFs);
        float nameTop = 0.f;
        float nameBottom = fs;
        float scoreTop = 0.f;
        float scoreBottom = scoreFs;
        ctx.measureTextVerticalBounds(row.player.name.c_str(), fs, nameTop, nameBottom);
        ctx.measureTextVerticalBounds(score, scoreFs, scoreTop, scoreBottom);
        const float nameHeight = nameBottom - nameTop;
        const float scoreHeight = scoreBottom - scoreTop;
        const float rowHeight = std::max({nameHeight, scoreHeight, cube});
        const float rowY = bgY + rows[i].offsetY * s;
        const float rowCenterY = rowY + rowHeight * 0.5f;
        const float cubeY = rowCenterY - cube * 0.5f;
        const float nameY = rowCenterY - (nameTop + nameBottom) * 0.5f;
        const float scoreY = rowCenterY - (scoreTop + scoreBottom) * 0.5f;
        const float rowWidth = cube + cubeGap + nameWidth + scoreGap + scoreWidth;
        if (showRowBorders) {
            ctx.rectOutline(rowX - borderPad,
                            rowY - borderPad,
                            rowWidth + borderPad * 2.f,
                            rowHeight + borderPad * 2.f,
                            std::max(1.f, 1.f * s),
                            withAlpha(k_textBright, 0.72f));
        }

        ctx.rect(rowX, cubeY, cube, cube, playerColor);
        ctx.rectOutline(rowX, cubeY, cube, cube, std::max(1.f, 1.f * s), withAlpha(k_textBright, 0.62f));

        const float nameX = rowX + cube + cubeGap;
        ctx.text(row.player.name.c_str(), nameX, nameY, fs, textColor, HudAlign::Left, true);

        const float scoreX = nameX + nameWidth + scoreGap;
        ctx.text(score, scoreX, scoreY, scoreFs, scoreColor, HudAlign::Left, true);
    }

    ctx.rotateVertices(rowsStart, bgCx, bgCy, rowsRotationDeg);
}
