/// @file MatchHeader.cpp
/// @brief Voidfall match-header implementation.

#include "MatchHeader.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

MatchHeader::MatchHeader()
{
    anchor = HudAnchor::TopCenter;
    offsetX = 0.f;
    offsetY = 16.f;
}

void MatchHeader::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    if (state.matchInfo.valid) {
        seconds_ = static_cast<int>(state.matchInfo.elapsedSeconds);
        fragTarget_ = state.matchInfo.fragTarget;
        useElapsed_ = true;
    } else {
        seconds_ = static_cast<int>(std::max(0.f, state.roundTimeRemaining));
        fragTarget_ = 30;
        useElapsed_ = false;
    }
}

void MatchHeader::draw(HudContext& ctx, float cx, float y)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float bigFs = timeFontSize * s;
    const float subFs = subFontSize * s;
    const float padX = panelPadX * s;
    const float padY = panelPadY * s;

    char timeBuf[16];
    SDL_snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", seconds_ / 60, seconds_ % 60);
    char subBuf[24];
    SDL_snprintf(subBuf, sizeof(subBuf), "FRAG %s %d", useElapsed_ ? "·" : "←", fragTarget_);

    const float timeW = ctx.measureText(timeBuf, bigFs);
    const float subW = ctx.measureText(subBuf, subFs);
    const float panelW = std::max(timeW, subW) + padX * 2.f;
    const float panelH = bigFs + subFs + padY * 2.f + 4.f * s;
    const float px = cx - panelW * 0.5f;
    const float py = y;

    drawPanel(ctx, px, py, panelW, panelH, k_bgPanel, k_line, 1.f);
    drawCornerBrackets(ctx, px, py, panelW, panelH, 12.f * s, 1.f * s, 3.f * s, k_amber);

    // Big mono time, white.
    const float timeY = py + padY;
    ctx.text(timeBuf, cx, timeY, bigFs, k_textBright, HudAlign::Center);

    // Subtitle: FRAG · <target>.
    const float subY = timeY + bigFs + 4.f * s - subFs * 0.18f;
    ctx.text(subBuf, cx, subY, subFs, k_textDim, HudAlign::Center);
}
