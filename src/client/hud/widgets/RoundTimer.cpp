/// @file RoundTimer.cpp
#include "RoundTimer.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>

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
    using namespace voidfall;

    const float s = uiScale_;
    const int minutes = static_cast<int>(timeRemaining_) / 60;
    const int seconds = static_cast<int>(timeRemaining_) % 60;

    char buf[16];
    SDL_snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);

    const HudColor color = (timeRemaining_ <= lowTimeThreshold) ? k_primary : k_textBright;

    ctx.text(buf, x, y, fontSize * s, color, HudAlign::Center);
}
