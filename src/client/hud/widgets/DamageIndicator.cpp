/// @file DamageIndicator.cpp
/// @brief Voidfall directional-damage arcs — red wedges around the screen edge
///        pointing at where the hit came from.

#include "DamageIndicator.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

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
    using namespace voidfall;

    const float s = uiScale_;

    for (const auto& a : arcs_) {
        // Fade-out alpha.
        const float t01 = std::clamp(a.timer / fadeTime, 0.f, 1.f);
        const float alpha = t01 * 0.85f;
        // Wedge glides from outside slightly inward as it fades — captures the
        // prototype's "blade-from-edge" feel on a fixed scale.
        const float radius = (180.f * (1.f - t01) + 200.f) * s;

        // Direction the damage came from (0° = front, +CW).
        const float rad = a.angleDeg * std::numbers::pi_v<float> / 180.f;
        const float dx = std::sin(rad);
        const float dy = -std::cos(rad);

        // Outer wedge (broad, more transparent).
        const float outerLen = arcLength * 1.4f * s;
        const float outerThk = arcThickness * 1.6f * s;
        const float ax1 = cx + dx * radius;
        const float ay1 = cy + dy * radius;
        // Tangent direction: perpendicular to radial.
        const float tx = dy;
        const float ty = -dx;
        // Approximate the curved blade with two stacked rotated rects: a wider
        // outer band and a narrower inner one closer to the edge.
        const float angDeg = a.angleDeg + 90.f; // align rect's long axis along the tangent
        ctx.rotatedRect(ax1, ay1, outerThk, outerLen, angDeg, withAlpha(k_red, alpha * 0.55f));

        // Inner wedge (thinner, more saturated).
        const float innerLen = arcLength * s;
        const float innerThk = arcThickness * 0.9f * s;
        const float innerR = radius - 10.f * s;
        const float ax2 = cx + dx * innerR;
        const float ay2 = cy + dy * innerR;
        ctx.rotatedRect(ax2, ay2, innerThk, innerLen, angDeg, withAlpha(k_red, alpha));

        // Tiny chevron tick at the outermost edge (matches design's chevron-tip).
        const float tipR = radius + 12.f * s;
        const float tx2 = cx + dx * tipR;
        const float ty2 = cy + dy * tipR;
        ctx.rotatedRect(tx2, ty2, 4.f * s, 4.f * s, a.angleDeg + 45.f, withAlpha(k_red, alpha));
        (void)tx;
        (void)ty;
    }
}
