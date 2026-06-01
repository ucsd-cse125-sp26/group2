/// @file Scoreboard.cpp
#include "Scoreboard.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

Scoreboard::Scoreboard()
{
    anchor = HudAnchor::Center;
    visible = false; // Off by default — toggled with TAB.
}

void Scoreboard::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    allies_.assign(state.allies.begin(), state.allies.end());
    enemies_.assign(state.enemies.begin(), state.enemies.end());
    allyScore_ = state.allyScore;
    enemyScore_ = state.enemyScore;
    visible = manualOpen_ || state.forceScoreboardOpen;
}

void Scoreboard::draw(HudContext& ctx, float cx, float cy)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float pw = panelWidth * s;
    const float ph = panelHeight * s;
    const float hfs = headerFontSize * s;
    const float rfs = rowFontSize * s;
    const float rh = rowHeight * s;

    const float x = cx - pw * 0.5f;
    const float y = cy - ph * 0.5f;

    // Background panel.
    ctx.rect(x, y, pw, ph, withAlpha(k_quaternary, 0.85f));
    ctx.rectOutline(x, y, pw, ph, 1.f * s, withAlpha(k_secondary, 0.80f));

    // Header.
    char header[64];
    SDL_snprintf(header, sizeof(header), "SCORE:  %d  -  %d", allyScore_, enemyScore_);
    ctx.text(header, cx, y + 10.f * s, hfs, k_textBright, HudAlign::Center);

    // Clip content area.
    ctx.pushClipRect(x + 4.f * s, y + 40.f * s, pw - 8.f * s, ph - 50.f * s);

    float rowY = y + 44.f * s;
    const float nameX = x + 10.f * s;
    const float killsX = x + pw * 0.6f;
    const float deathsX = x + pw * 0.7f;
    const float pingX = x + pw * 0.85f;

    // Column headers.
    const HudColor headerCol = k_textDim;
    ctx.text("Name", nameX, rowY, rfs, headerCol);
    ctx.text("K", killsX, rowY, rfs, headerCol);
    ctx.text("D", deathsX, rowY, rfs, headerCol);
    ctx.text("Ping", pingX, rowY, rfs, headerCol);
    rowY += rh;

    // Allies.
    for (const auto& a : allies_) {
        const HudColor c = a.isAlive ? k_tertiary : withAlpha(k_secondary, 0.7f);
        ctx.text(a.name.c_str(), nameX, rowY, rfs, c);
        char buf[16];
        SDL_snprintf(buf, sizeof(buf), "%d", a.kills);
        ctx.text(buf, killsX, rowY, rfs, c);
        SDL_snprintf(buf, sizeof(buf), "%d", a.deaths);
        ctx.text(buf, deathsX, rowY, rfs, c);
        SDL_snprintf(buf, sizeof(buf), "%d", a.ping);
        ctx.text(buf, pingX, rowY, rfs, c);
        rowY += rh;
    }

    // Divider.
    rowY += 4.f * s;
    ctx.rect(x + 10.f * s, rowY, pw - 20.f * s, 1.f * s, withAlpha(k_secondary, 0.5f));
    rowY += 6.f * s;

    // Enemies.
    for (const auto& e : enemies_) {
        const HudColor c = e.isAlive ? k_primary : withAlpha(k_secondary, 0.7f);
        ctx.text(e.name.c_str(), nameX, rowY, rfs, c);
        char buf[16];
        SDL_snprintf(buf, sizeof(buf), "%d", e.kills);
        ctx.text(buf, killsX, rowY, rfs, c);
        SDL_snprintf(buf, sizeof(buf), "%d", e.deaths);
        ctx.text(buf, deathsX, rowY, rfs, c);
        SDL_snprintf(buf, sizeof(buf), "%d", e.ping);
        ctx.text(buf, pingX, rowY, rfs, c);
        rowY += rh;
    }

    ctx.popClipRect();
}
