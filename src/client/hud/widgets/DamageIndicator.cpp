/// @file DamageIndicator.cpp
#include "DamageIndicator.hpp"

#include "hud/HudContext.hpp"

#include <algorithm>
#include <cmath>

DamageIndicator::DamageIndicator()
{
    anchor = HudAnchor::Center;
}

void DamageIndicator::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    for (const auto& ev : state.damageEvents)
        arcs_.push_back({ev.angleDeg, fadeTime});

    for (auto& a : arcs_)
        a.timer -= dt;
    arcs_.erase(std::remove_if(arcs_.begin(), arcs_.end(), [](const Arc& a) { return a.timer <= 0.f; }), arcs_.end());
}

void DamageIndicator::draw(HudContext& ctx, float cx, float cy)
{
    const float s = uiScale_;
    for (const auto& a : arcs_) {
        const float alpha = std::clamp(a.timer / fadeTime, 0.f, 1.f);
        const float rad = glm::radians(a.angleDeg);
        // Direction toward damage source.
        const float dx = std::sin(rad);
        const float dy = -std::cos(rad);
        // Arc center position.
        const float dist = arcDistance * s;
        const float ax = cx + dx * dist;
        const float ay = cy + dy * dist;
        const float al = arcLength * s;
        const float at = arcThickness * s;
        // Draw a small rect oriented toward the damage direction.
        ctx.rect(ax - at * 0.5f, ay - al * 0.5f, at, al, HudColor(1.f, 0.1f, 0.1f, alpha * 0.7f));
    }
}
