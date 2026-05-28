/// @file RailgunScopeWidget.cpp
/// @brief Railgun scope overlay and charge readout.

#include "RailgunScopeWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace voidfall;

namespace
{
constexpr float kPi = 3.14159265358979323846f;

float degToRad(float deg)
{
    return deg * kPi / 180.f;
}

void drawArc(
    HudContext& ctx, float cx, float cy, float radius, float startDeg, float endDeg, float thickness, HudColor color)
{
    const float sweep = endDeg - startDeg;
    const int steps = std::max(2, static_cast<int>(std::ceil(std::abs(sweep) / 6.f)));
    for (int i = 0; i < steps; ++i) {
        const float a0 = degToRad(startDeg + sweep * (static_cast<float>(i) / steps));
        const float a1 = degToRad(startDeg + sweep * (static_cast<float>(i + 1) / steps));
        const float points[4] = {
            cx + std::cos(a0) * radius,
            cy + std::sin(a0) * radius,
            cx + std::cos(a1) * radius,
            cy + std::sin(a1) * radius,
        };
        ctx.polyline(points, 2, thickness, color);
    }
}

void drawRadialTick(HudContext& ctx,
                    float cx,
                    float cy,
                    float deg,
                    float innerRadius,
                    float outerRadius,
                    float thickness,
                    HudColor color)
{
    const float a = degToRad(deg);
    const float ca = std::cos(a);
    const float sa = std::sin(a);
    const float points[4] = {
        cx + ca * innerRadius,
        cy + sa * innerRadius,
        cx + ca * outerRadius,
        cy + sa * outerRadius,
    };
    ctx.polyline(points, 2, thickness, color);
}
} // namespace

RailgunScopeWidget::RailgunScopeWidget()
{
    anchor = HudAnchor::TopLeft;
    visible = false;
}

void RailgunScopeWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    screenW_ = state.screenW;
    screenH_ = state.screenH;
    chargeTime_ = state.railgunChargeTime;
    visible = state.isAlive && state.railgunScoped;
}

void RailgunScopeWidget::draw(HudContext& ctx, float /*drawX*/, float /*drawY*/)
{
    const float s = uiScale_;
    const float cx = screenW_ * 0.5f;
    const float cy = screenH_ * 0.5f;
    const float radius = std::min(screenW_, screenH_) * 0.42f;

    // Vignette outside the scope circle. The scene renders at a narrower FOV
    // while ADS-ing (driven by renderer->scopeZoom), so the off-scope corners
    // show a zoomed-in view too — this darker mask softens the visual seam
    // without going opaque (the user still wants peripheral awareness).
    ctx.scopeMask(screenW_, screenH_, radius, withAlpha(k_quaternary, 0.62f));

    const HudColor primary = withAlpha(k_primary, 0.82f);
    const HudColor primaryDim = withAlpha(k_primary, 0.45f);
    const HudColor pale = withAlpha(k_tertiary, 0.58f);
    const HudColor dark = withAlpha(k_quaternary, 0.76f);

    // Heavy broken outside bands echo the reference scope glass and make the
    // transparent cut-out read as an optic instead of a plain vignette.
    drawArc(ctx, cx, cy, radius + 4.f * s, 112.f, 158.f, 12.f * s, dark);
    drawArc(ctx, cx, cy, radius + 4.f * s, 202.f, 248.f, 12.f * s, dark);
    drawArc(ctx, cx, cy, radius + 4.f * s, -68.f, -22.f, 12.f * s, dark);
    drawArc(ctx, cx, cy, radius + 4.f * s, 22.f, 68.f, 12.f * s, dark);

    drawArc(ctx, cx, cy, radius - 10.f * s, 0.f, 360.f, 1.5f * s, pale);
    drawArc(ctx, cx, cy, radius - 24.f * s, 20.f, 160.f, 1.5f * s, primaryDim);
    drawArc(ctx, cx, cy, radius - 24.f * s, 200.f, 340.f, 1.5f * s, primaryDim);

    for (int deg = 0; deg < 360; deg += 5) {
        const bool major = deg % 30 == 0;
        const bool cardinal = deg % 90 == 0;
        const float tickLen = (cardinal ? 17.f : major ? 12.f : 7.f) * s;
        const float thickness = (cardinal ? 2.4f : major ? 1.8f : 1.0f) * s;
        const HudColor color = major ? primary : pale;
        drawRadialTick(
            ctx, cx, cy, static_cast<float>(deg), radius - 34.f * s, radius - 34.f * s + tickLen, thickness, color);
    }

    const float lineR = radius - 58.f * s;
    const float gap = 18.f * s;
    const float lineT = 1.25f * s;
    ctx.rect(cx - lineR, cy - lineT * 0.5f, lineR - gap, lineT, withAlpha(k_tertiary, 0.42f));
    ctx.rect(cx + gap, cy - lineT * 0.5f, lineR - gap, lineT, withAlpha(k_tertiary, 0.42f));
    ctx.rect(cx - lineT * 0.5f, cy - lineR, lineT, lineR - gap, withAlpha(k_tertiary, 0.30f));
    ctx.rect(cx - lineT * 0.5f, cy + gap, lineT, lineR - gap, withAlpha(k_tertiary, 0.30f));

    drawArc(ctx, cx, cy, 42.f * s, 112.f, 248.f, 2.f * s, primary);
    drawArc(ctx, cx, cy, 42.f * s, -68.f, 68.f, 2.f * s, primary);

    const float d = 8.f * s;
    const float diamond[10] = {cx, cy - d, cx + d, cy, cx, cy + d, cx - d, cy, cx, cy - d};
    ctx.polyline(diamond, 5, 1.5f * s, withAlpha(k_tertiary, 0.82f));

    char charge[16];
    std::snprintf(charge, sizeof(charge), "%.2f", static_cast<double>(chargeTime_));
    ctx.text(charge, cx, cy + 52.f * s, 26.f * s, withAlpha(k_primary, 0.92f), HudAlign::Center, true);
}
