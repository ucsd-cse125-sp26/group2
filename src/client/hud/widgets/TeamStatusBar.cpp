/// @file TeamStatusBar.cpp
#include "TeamStatusBar.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

TeamStatusBar::TeamStatusBar()
{
    anchor = HudAnchor::TopCenter;
    offsetX = 0.f;
    offsetY = 40.f; // Below RoundTimer.
}

void TeamStatusBar::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    allyTotal_ = static_cast<int>(state.allies.size());
    allyAlive_ = 0;
    for (const auto& a : state.allies)
        if (a.isAlive)
            allyAlive_++;

    enemyTotal_ = static_cast<int>(state.enemies.size());
    enemyAlive_ = 0;
    for (const auto& e : state.enemies)
        if (e.isAlive)
            enemyAlive_++;

    allyScore_ = state.allyScore;
    enemyScore_ = state.enemyScore;
}

void TeamStatusBar::draw(HudContext& ctx, float x, float y)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float sfs = scoreFontSize * s;
    const float isz = indicatorSize * s;
    const float isp = indicatorSpacing * s;

    // Score: "AllyScore - EnemyScore" centered.
    char scoreText[32];
    SDL_snprintf(scoreText, sizeof(scoreText), "%d - %d", allyScore_, enemyScore_);
    ctx.text(scoreText, x, y, sfs, k_textBright, HudAlign::Center);

    // Ally indicators (left of center).
    const float indicatorY = y + sfs + 4.f * s;
    float curX = x - (static_cast<float>(allyTotal_) * (isz + isp)) * 0.5f;
    for (int i = 0; i < allyTotal_; i++) {
        const bool alive = i < allyAlive_;
        ctx.rect(curX, indicatorY, isz, isz, alive ? withAlpha(k_tertiary, 0.9f) : withAlpha(k_secondary, 0.5f));
        curX += isz + isp;
    }
}
