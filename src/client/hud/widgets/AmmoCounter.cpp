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
    const float s = uiScale_;
    const float cfs = clipFontSize * s;
    const float rfs = reserveFontSize * s;
    const float dp = dividerPadding * s;

    char clipText[16];
    SDL_snprintf(clipText, sizeof(clipText), "%d", displayClip_);

    char reserveText[16];
    SDL_snprintf(reserveText, sizeof(reserveText), "/ %d", displayReserve_);

    // Clip count (large, right-aligned from anchor).
    const float clipW = ctx.measureText(clipText, cfs);
    ctx.text(clipText, x - clipW, y, cfs, HudColor::white());

    // Reserve count (smaller, to the LEFT of clip text, right-aligned).
    const float reserveW = ctx.measureText(reserveText, rfs);
    ctx.text(reserveText, x - clipW - dp - reserveW, y + cfs - rfs, rfs, HudColor(0.7f, 0.7f, 0.7f, 0.8f));
}
