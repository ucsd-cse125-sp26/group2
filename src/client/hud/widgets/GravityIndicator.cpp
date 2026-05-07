/// @file GravityIndicator.cpp
/// @brief Voidfall gravity widget — square panel, dotted ring, amber arrow.

#include "GravityIndicator.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <cmath>
#include <numbers>

GravityIndicator::GravityIndicator()
{
    anchor = HudAnchor::BottomRight;
    offsetX = -24.f;
    offsetY = -200.f; // sits above the weapon panel
}

void GravityIndicator::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    direction_ = state.gravityDirection;
}

void GravityIndicator::draw(HudContext& ctx, float anchorX, float anchorY)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float ds = diskSize * s;
    const float x = anchorX - ds;
    const float y = anchorY - ds;
    const float cx = x + ds * 0.5f;
    const float cy = y + ds * 0.5f;

    drawPanel(ctx, x, y, ds, ds, k_bgPanel, k_line, 1.f);

    // Outer dotted ring (dashed circle outline).
    {
        const float r = ds * 0.36f;
        const int dashes = 24;
        const float t = 1.0f * s;
        for (int i = 0; i < dashes; ++i) {
            // Skip every other tick to give a dashed effect.
            if (i % 2 == 0)
                continue;
            const float a = static_cast<float>(i) / dashes * (2.f * std::numbers::pi_v<float>);
            const float bx = cx + std::cos(a) * r;
            const float by = cy + std::sin(a) * r;
            ctx.rotatedRect(bx, by, t, 4.f * s, (a * 180.f / std::numbers::pi_v<float>)+90.f, k_lineDim);
        }
    }

    // Inner faint ring.
    {
        const float r = ds * 0.25f;
        const int segs = 24;
        const float t = 0.7f * s;
        for (int i = 0; i < segs; ++i) {
            const float a = static_cast<float>(i) / segs * (2.f * std::numbers::pi_v<float>);
            const float bx = cx + std::cos(a) * r;
            const float by = cy + std::sin(a) * r;
            ctx.rotatedRect(bx, by, t, 2.f * s, (a * 180.f / std::numbers::pi_v<float>)+90.f, k_lineDim);
        }
    }

    // Amber arrow pointing in the gravity direction.
    const float angleDeg = static_cast<float>(direction_) * 90.f; // 0=down, 1=left, 2=up, 3=right
    const float arrowLen = ds * 0.45f;
    const float arrowThk = 1.8f * s;
    // Body.
    ctx.rotatedRect(cx, cy, arrowThk, arrowLen, angleDeg, k_amber);
    // Head: two short rotated rects forming a chevron tip pointing in the
    // arrow direction.  We rotate by (angle ± 45°) and offset the chevron
    // tip toward the head end of the body.
    const float radLeg = (angleDeg + 90.f) * std::numbers::pi_v<float> / 180.f;
    const float dxL = std::cos(radLeg);
    const float dyL = std::sin(radLeg);
    (void)dxL;
    (void)dyL;
    // Compute the head endpoint along the body direction.
    const float bodyRad = angleDeg * std::numbers::pi_v<float> / 180.f;
    const float headX = cx + std::sin(bodyRad) * arrowLen * 0.5f;
    const float headY = cy + std::cos(bodyRad) * arrowLen * 0.5f;
    const float legLen = arrowLen * 0.40f;
    ctx.rotatedRect(headX - std::sin(bodyRad) * legLen * 0.30f,
                    headY - std::cos(bodyRad) * legLen * 0.30f,
                    arrowThk,
                    legLen,
                    angleDeg + 45.f,
                    k_amber);
    ctx.rotatedRect(headX - std::sin(bodyRad) * legLen * 0.30f,
                    headY - std::cos(bodyRad) * legLen * 0.30f,
                    arrowThk,
                    legLen,
                    angleDeg - 45.f,
                    k_amber);
}
