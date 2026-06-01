/// @file PrematchBanner.hpp
/// @brief Match phase overlay: warmup waiting message, countdown integer, and post-match result.
#pragma once

#include "hud/HudWidget.hpp"
#include "network/MatchStatus.hpp"

struct PrematchBanner : HudWidget
{
    PrematchBanner();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    MatchPhase phase_ = MatchPhase::IN_PROGRESS;
    float timeRemaining_ = 0.f;
    bool matchWon_ = false;
};
