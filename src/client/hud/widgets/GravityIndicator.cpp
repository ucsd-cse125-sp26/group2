/// @file GravityIndicator.cpp
/// @brief Voidfall gravity widget — square panel, dotted ring, amber arrow.

#include "GravityIndicator.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudIcons.hpp"
#include "hud/VoidfallStyle.hpp"

#include <cmath>
#include <numbers>

GravityIndicator::GravityIndicator()
{
    anchor = HudAnchor::BottomRight;
    offsetX = -80.f;
    offsetY = -220.f; // sits above the weapon panel
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

    // Outer dotted ring (every-other tick missing for the dashed effect).
    {
        const float r = ds * 0.36f;
        const int dashes = 24;
        const float t = 1.0f * s;
        for (int i = 0; i < dashes; ++i) {
            if (i % 2 == 0)
                continue;
            const float a = static_cast<float>(i) / static_cast<float>(dashes) * (2.f * std::numbers::pi_v<float>);
            const float bx = cx + std::cos(a) * r;
            const float by = cy + std::sin(a) * r;
            ctx.rotatedRect(bx, by, t, 4.f * s, (a * 180.f / std::numbers::pi_v<float>)+90.f, k_lineDim);
        }
    }

    // Inner faint ring drawn via the icon module's stroked-circle helper for
    // consistency with the rest of the HUD's circular geometry.
    icons::strokedCircle(ctx, cx, cy, ds * 0.25f, 0.7f * s, 24, k_lineDim);

    // Amber arrow pointing in the gravity direction.  Routed through the
    // shared icon module so this glyph stays consistent with anywhere else
    // a directional arrow shows up.
    icons::gravityArrow(ctx, x, y, ds, direction_, k_amber);
}
