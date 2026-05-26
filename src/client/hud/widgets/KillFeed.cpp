/// @file KillFeed.cpp
/// @brief Voidfall kill-feed implementation.

#include "KillFeed.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace
{

bool nameMatchesYou(const std::string& s)
{
    return s == "You" || s == "YOU";
}

void drawCircle(HudContext& ctx, float cx, float cy, float radius, HudColor color)
{
    constexpr int kSegments = 20;
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(kSegments)) * 2.f * 3.14159265f;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(kSegments)) * 2.f * 3.14159265f;
        ctx.triangle(cx, cy, cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, color);
    }
}

void drawSwords(HudContext& ctx, float cx, float cy, float size, HudColor color)
{
    ctx.rotatedRect(cx - size * 0.10f, cy, size * 0.12f, size * 0.90f, 45.f, color);
    ctx.rotatedRect(cx + size * 0.10f, cy, size * 0.12f, size * 0.90f, -45.f, color);
    ctx.rotatedRect(cx - size * 0.24f, cy + size * 0.24f, size * 0.08f, size * 0.34f, 45.f, color);
    ctx.rotatedRect(cx + size * 0.24f, cy + size * 0.24f, size * 0.08f, size * 0.34f, -45.f, color);
}

} // namespace

KillFeed::KillFeed()
{
    anchor = HudAnchor::TopRight;
    offsetX = -60.f;
    offsetY = 55.f;
}

void KillFeed::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    allyScore_ = state.allyScore;
    enemyScore_ = state.enemyScore;
    localScore_ = state.kda.kills * 100 + state.kda.assists * 50;

    for (const auto& ev : state.killFeedEvents) {
        Entry e;
        e.killerName = ev.killerName;
        e.victimName = ev.victimName;
        e.isHeadshot = ev.isHeadshot;
        e.youAreKiller = nameMatchesYou(ev.killerName);
        e.youAreVictim = nameMatchesYou(ev.victimName);
        e.timer = entryLifetime;
        e.slideIn = 0.f;
        entries_.insert(entries_.begin(), e);
    }

    for (auto& e : entries_) {
        if (e.permanent)
            continue;
        e.timer -= dt;
        e.slideIn = std::min(1.f, e.slideIn + dt * 6.f); // ~0.16s slide-in
    }
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.permanent && e.timer <= 0.f; }),
        entries_.end());

    while (static_cast<int>(entries_.size()) > maxEntries) {
        const auto removable =
            std::find_if(entries_.rbegin(), entries_.rend(), [](const Entry& e) { return !e.permanent; });
        if (removable == entries_.rend())
            break;
        entries_.erase(std::next(removable).base());
    }
}

void KillFeed::draw(HudContext& ctx, float anchorX, float y)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float fs = fontSize * s;
    const float eh = entryHeight * s;
    const float padX = 24.f * s;
    const float panelW = panelWidth * s;
    const float lbH = leaderboardHeight * s;
    const float feedH = feedHeight * s;
    const float x = anchorX - panelW;

    drawHoloPanel(ctx,
                  x,
                  y,
                  panelW,
                  lbH,
                  18.f * s,
                  withAlpha(k_bgPanelSolid, 0.68f),
                  withAlpha(k_bgPanel, 0.50f),
                  withAlpha(k_lineBright, 0.78f),
                  std::max(1.f, 2.f * s));

    char bestScore[32];
    SDL_snprintf(bestScore, sizeof(bestScore), "%d", std::max(allyScore_, enemyScore_));
    char youScore[32];
    SDL_snprintf(youScore, sizeof(youScore), "%d", localScore_);
    const float row1 = y + 30.f * s;
    const float row2 = y + 83.f * s;
    const float labelX = x + 74.f * s;
    const float dotX = x + 214.f * s;
    const float valueX = x + 250.f * s;
    ctx.text("BEST", labelX, row1, fs, k_textBright, HudAlign::Left, true);
    drawCircle(ctx, dotX, row1 + fs * 0.48f, 18.f * s, k_primary);
    ctx.text(": LEADER -", valueX, row1, fs, k_textBright, HudAlign::Left, true);
    ctx.text(bestScore, x + panelW - 24.f * s, row1, fs, k_textBright, HudAlign::Right, true);
    ctx.text("YOU", labelX + 18.f * s, row2, fs, k_textBright, HudAlign::Left, true);
    drawCircle(ctx, dotX, row2 + fs * 0.48f, 18.f * s, HudColor{1.f, 0.46f, 0.26f, 1.f});
    ctx.text(": PLAYER -", valueX, row2, fs, k_textBright, HudAlign::Left, true);
    ctx.text(youScore, x + panelW - 24.f * s, row2, fs, k_textBright, HudAlign::Right, true);

    const float feedY = y + lbH + feedGap * s;
    drawHoloPanel(ctx,
                  x,
                  feedY,
                  panelW,
                  feedH,
                  18.f * s,
                  withAlpha(k_bgPanelSolid, 0.54f),
                  withAlpha(k_bgPanel, 0.42f),
                  withAlpha(k_lineBright, 0.50f),
                  std::max(1.f, 1.5f * s));

    const float lineX = x + 18.f * s;
    const float lineW = panelW - 36.f * s;
    for (int i = 1; i < maxEntries; ++i)
        ctx.rect(lineX, feedY + static_cast<float>(i) * eh, lineW, std::max(1.f, s), withAlpha(k_lineDim, 0.23f));

    float curY = feedY;
    int drawn = 0;
    for (const auto& e : entries_) {
        if (drawn >= maxEntries)
            break;
        const float alpha = (e.permanent ? 1.f : std::min(e.timer / fadeOutDuration, 1.f)) * e.slideIn;
        const float slideOff = (1.f - e.slideIn) * 18.f * s;
        const float rowY = curY + (eh - fs) * 0.5f - fs * 0.18f;
        ctx.text(e.killerName.c_str(), x + padX - slideOff, rowY, fs, withAlpha(k_textBright, alpha), HudAlign::Left, true);
        drawSwords(ctx, x + panelW * 0.50f, curY + eh * 0.52f, 28.f * s, withAlpha(k_textBright, alpha));
        ctx.text(e.victimName.c_str(), x + panelW - padX + slideOff, rowY, fs, withAlpha(k_textBright, alpha), HudAlign::Right, true);
        curY += eh;
        ++drawn;
    }
}
