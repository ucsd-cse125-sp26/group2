/// @file Minimap.cpp
#include "Minimap.hpp"

#include "hud/HudContext.hpp"

Minimap::Minimap()
{
    anchor = HudAnchor::TopLeft;
    offsetX = 10.f;
    offsetY = 80.f; // Below TeamStatusBar area.
    width = 150.f;
    height = 150.f;
}

void Minimap::update(float /*dt*/, const HudGameState& /*state*/, HudTweenPool& /*tweens*/)
{
    // Teammate/enemy positions would be added to HudGameState
    // and rendered as pips here.  For now, just the frame.
}

void Minimap::draw(HudContext& ctx, float x, float y)
{
    // Background.
    ctx.rect(x, y, mapSize, mapSize, HudColor(0.05f, 0.08f, 0.05f, 0.6f));
    ctx.rectOutline(x, y, mapSize, mapSize, borderThickness, HudColor(0.3f, 0.5f, 0.3f, 0.8f));

    // Player dot (always center).
    const float cx = x + mapSize * 0.5f;
    const float cy = y + mapSize * 0.5f;
    ctx.rect(cx - dotSize * 0.5f, cy - dotSize * 0.5f, dotSize, dotSize, HudColor(0.f, 1.f, 0.f, 1.f));
}
