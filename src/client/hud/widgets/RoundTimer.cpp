/// @file RoundTimer.cpp
#include "RoundTimer.hpp"

#include "hud/HudContext.hpp"

#include <algorithm>
#include <cstdio>

RoundTimer::RoundTimer()
{
    anchor = HudAnchor::TopCenter;
    offsetX = 0.f;
    offsetY = 10.f;
}

void RoundTimer::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    timeRemaining_ = std::max(state.roundTimeRemaining, 0.f);
}

void RoundTimer::draw(HudContext& ctx, float x, float y)
{
    const int minutes = static_cast<int>(timeRemaining_) / 60;
    const int seconds = static_cast<int>(timeRemaining_) % 60;

    char buf[16];
    SDL_snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);

    const HudColor color = (timeRemaining_ <= lowTimeThreshold) ? HudColor(1.f, 0.2f, 0.2f, 1.f) : HudColor::white();

    ctx.text(buf, x, y, fontSize, color, HudAlign::Center);
}
