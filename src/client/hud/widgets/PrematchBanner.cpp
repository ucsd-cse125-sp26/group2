/// @file PrematchBanner.cpp
#include "PrematchBanner.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>

PrematchBanner::PrematchBanner()
{
    anchor = HudAnchor::BottomCenter;
    offsetX = 0.f;
    offsetY = -260.f;
}

void PrematchBanner::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    phase_ = state.currentPhase;
    timeRemaining_ = std::max(state.roundTimeRemaining, 0.f);
    matchWon_ = state.matchWon;

    visible = (phase_ == MatchPhase::WARMUP || phase_ == MatchPhase::COUNTDOWN || phase_ == MatchPhase::FINISHED);

    if (phase_ == MatchPhase::WARMUP) {
        anchor = HudAnchor::BottomCenter;
        offsetX = 0.f;
        offsetY = -260.f;
    } else if (phase_ == MatchPhase::COUNTDOWN || phase_ == MatchPhase::FINISHED) {
        anchor = HudAnchor::Center;
        offsetX = 0.f;
        offsetY = 0.f;
    }
}

void PrematchBanner::draw(HudContext& ctx, float x, float y)
{
    using namespace voidfall;

    const float s = uiScale_;

    if (phase_ == MatchPhase::WARMUP) {
        ctx.text("Waiting for players to join...", x, y, 28.f * s, k_textBright, HudAlign::Center);
    } else if (phase_ == MatchPhase::COUNTDOWN) {
        char buf[8];
        const int n = std::max(1, static_cast<int>(std::ceil(timeRemaining_)));
        SDL_snprintf(buf, sizeof(buf), "%d", n);
        ctx.text(buf, x, y, 160.f * s, k_primary, HudAlign::Center);
    } else if (phase_ == MatchPhase::FINISHED) {
        constexpr HudColor k_victoryGreen{0.20f, 0.95f, 0.45f, 1.0f};
        constexpr HudColor k_defeatRed{0.95f, 0.18f, 0.16f, 1.0f};
        ctx.text(matchWon_ ? "VICTORY" : "DEFEAT",
                 x,
                 y,
                 96.f * s,
                 matchWon_ ? k_victoryGreen : k_defeatRed,
                 HudAlign::Center);
    }
}
