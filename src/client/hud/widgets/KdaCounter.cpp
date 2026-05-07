/// @file KdaCounter.cpp
/// @brief Voidfall K/A/D counter implementation.

#include "KdaCounter.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

KdaCounter::KdaCounter()
{
    anchor = HudAnchor::TopRight;
    offsetX = -20.f;
    offsetY = 20.f;
}

void KdaCounter::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    kills_ = state.kda.kills;
    assists_ = state.kda.assists;
    deaths_ = state.kda.deaths;
    visible = state.isAlive || (kills_ + assists_ + deaths_) > 0;
}

void KdaCounter::draw(HudContext& ctx, float anchorX, float y)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float kFs = kFontSize * s;
    const float adFs = adFontSize * s;
    const float labFs = labelFontSize * s;
    const float padX = panelPadX * s;
    const float padY = panelPadY * s;
    const float colGapPx = colGap * s;

    char kBuf[8], aBuf[8], dBuf[8];
    SDL_snprintf(kBuf, sizeof(kBuf), "%d", kills_);
    SDL_snprintf(aBuf, sizeof(aBuf), "%d", assists_);
    SDL_snprintf(dBuf, sizeof(dBuf), "%d", deaths_);

    // Compute column widths from text metrics so the panel hugs content.
    const float kW = std::max(ctx.measureText(kBuf, kFs), ctx.measureText("K", labFs));
    const float aW = std::max(ctx.measureText(aBuf, adFs), ctx.measureText("A", labFs));
    const float dW = std::max(ctx.measureText(dBuf, adFs), ctx.measureText("D", labFs));
    const float divW = 1.f * s;

    const float contentW = kW + colGapPx + divW + colGapPx + aW + colGapPx + divW + colGapPx + dW;
    const float panelW = contentW + padX * 2.f;
    const float panelH = kFs + labFs + padY * 2.f + 2.f * s;
    const float px = anchorX - panelW;
    const float py = y;

    drawPanel(ctx, px, py, panelW, panelH, k_bgPanel, k_line, 1.f);
    drawCornerBrackets(ctx, px, py, panelW, panelH, 10.f * s, 1.f * s, 2.f * s, k_amber);

    // Layout columns left-to-right.
    float curX = px + padX;
    auto drawCol = [&](const char* num, float numFs, HudColor numC, const char* lbl, float colW) {
        // Number on top.
        ctx.text(num, curX + colW * 0.5f, py + padY, numFs, numC, HudAlign::Center);
        // Label underneath.
        ctx.text(lbl, curX + colW * 0.5f, py + padY + numFs + 1.f * s, labFs, k_textDim, HudAlign::Center);
        curX += colW;
    };

    drawCol(kBuf, kFs, k_amber, "K", kW);
    curX += colGapPx;
    ctx.rect(curX, py + padY, divW, kFs + labFs - 4.f * s, k_lineDim);
    curX += divW + colGapPx;
    // A and D align baselines with K (centered in the bigger row).
    drawCol(aBuf, adFs, k_textBright, "A", aW);
    curX += colGapPx;
    ctx.rect(curX, py + padY, divW, kFs + labFs - 4.f * s, k_lineDim);
    curX += divW + colGapPx;
    drawCol(dBuf, adFs, k_textDim, "D", dW);
}
