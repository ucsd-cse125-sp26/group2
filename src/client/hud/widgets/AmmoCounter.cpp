/// @file AmmoCounter.cpp
#include "AmmoCounter.hpp"

#include "hud/HudContext.hpp"

#include <SDL3/SDL.h>

AmmoCounter::AmmoCounter()
{
    anchor = HudAnchor::BottomRight;
    offsetX = -20.f;
    offsetY = -50.f;
}

void AmmoCounter::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;
    displayClip_ = state.ammoClip;
    displayReserve_ = state.ammoReserve;
}

void AmmoCounter::draw(HudContext& ctx, float x, float y)
{
    char clipText[16];
    SDL_snprintf(clipText, sizeof(clipText), "%d", displayClip_);

    char reserveText[16];
    SDL_snprintf(reserveText, sizeof(reserveText), "/ %d", displayReserve_);

    // Clip count (large, right-aligned from anchor).
    const float clipW = ctx.measureText(clipText, clipFontSize);
    ctx.text(clipText, x - clipW, y, clipFontSize, HudColor::white());

    // Reserve count (smaller, left of clip).
    ctx.text(reserveText,
             x - clipW - dividerPadding,
             y + clipFontSize - reserveFontSize,
             reserveFontSize,
             HudColor(0.7f, 0.7f, 0.7f, 0.8f));
}
