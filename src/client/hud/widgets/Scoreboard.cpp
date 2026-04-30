/// @file Scoreboard.cpp
#include "Scoreboard.hpp"

#include "hud/HudContext.hpp"

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
}

void Scoreboard::draw(HudContext& ctx, float cx, float cy)
{
    const float x = cx - panelWidth * 0.5f;
    const float y = cy - panelHeight * 0.5f;

    // Background panel.
    ctx.rect(x, y, panelWidth, panelHeight, HudColor(0.05f, 0.05f, 0.1f, 0.85f));
    ctx.rectOutline(x, y, panelWidth, panelHeight, 1.f, HudColor(0.4f, 0.4f, 0.5f, 0.8f));

    // Header.
    char header[64];
    SDL_snprintf(header, sizeof(header), "SCORE:  %d  -  %d", allyScore_, enemyScore_);
    ctx.text(header, cx, y + 10.f, headerFontSize, HudColor::white(), HudAlign::Center);

    // Clip content area.
    ctx.pushClipRect(x + 4.f, y + 40.f, panelWidth - 8.f, panelHeight - 50.f);

    float rowY = y + 44.f;
    const float nameX = x + 10.f;
    const float killsX = x + panelWidth * 0.6f;
    const float deathsX = x + panelWidth * 0.7f;
    const float pingX = x + panelWidth * 0.85f;

    // Column headers.
    ctx.text("Name", nameX, rowY, rowFontSize, HudColor(0.7f, 0.7f, 0.7f, 1.f));
    ctx.text("K", killsX, rowY, rowFontSize, HudColor(0.7f, 0.7f, 0.7f, 1.f));
    ctx.text("D", deathsX, rowY, rowFontSize, HudColor(0.7f, 0.7f, 0.7f, 1.f));
    ctx.text("Ping", pingX, rowY, rowFontSize, HudColor(0.7f, 0.7f, 0.7f, 1.f));
    rowY += rowHeight;

    // Allies.
    for (const auto& a : allies_) {
        const HudColor c = a.isAlive ? HudColor(0.3f, 0.7f, 1.f, 1.f) : HudColor(0.4f, 0.4f, 0.4f, 0.7f);
        ctx.text(a.name.c_str(), nameX, rowY, rowFontSize, c);
        char buf[16];
        SDL_snprintf(buf, sizeof(buf), "%d", a.kills);
        ctx.text(buf, killsX, rowY, rowFontSize, c);
        SDL_snprintf(buf, sizeof(buf), "%d", a.deaths);
        ctx.text(buf, deathsX, rowY, rowFontSize, c);
        SDL_snprintf(buf, sizeof(buf), "%d", a.ping);
        ctx.text(buf, pingX, rowY, rowFontSize, c);
        rowY += rowHeight;
    }

    // Divider.
    rowY += 4.f;
    ctx.rect(x + 10.f, rowY, panelWidth - 20.f, 1.f, HudColor(0.5f, 0.5f, 0.5f, 0.5f));
    rowY += 6.f;

    // Enemies.
    for (const auto& e : enemies_) {
        const HudColor c = e.isAlive ? HudColor(1.f, 0.4f, 0.3f, 1.f) : HudColor(0.4f, 0.4f, 0.4f, 0.7f);
        ctx.text(e.name.c_str(), nameX, rowY, rowFontSize, c);
        char buf[16];
        SDL_snprintf(buf, sizeof(buf), "%d", e.kills);
        ctx.text(buf, killsX, rowY, rowFontSize, c);
        SDL_snprintf(buf, sizeof(buf), "%d", e.deaths);
        ctx.text(buf, deathsX, rowY, rowFontSize, c);
        SDL_snprintf(buf, sizeof(buf), "%d", e.ping);
        ctx.text(buf, pingX, rowY, rowFontSize, c);
        rowY += rowHeight;
    }

    ctx.popClipRect();
}
