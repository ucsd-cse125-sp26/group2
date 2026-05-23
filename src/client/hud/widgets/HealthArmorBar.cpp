/// @file HealthArmorBar.cpp
/// @brief Layered health and armor silhouette.

#include "HealthArmorBar.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
struct Point
{
    float x = 0.f;
    float y = 0.f;
};

std::array<Point, 6> barShape(float x, float y, float w, float h, float chamfer, float cornerCut)
{
    const float cut = std::clamp(chamfer, 0.f, w * 0.45f);
    const float topCut = std::clamp(cornerCut, 0.f, std::min(cut, h * 0.5f));
    return {{
        {x + topCut, y},
        {x + w - topCut, y},
        {x + w, y + topCut},
        {x + w - cut, y + h},
        {x + cut, y + h},
        {x, y + topCut},
    }};
}

std::vector<Point> clipLeftToRight(const std::array<Point, 6>& points, float clipX)
{
    std::vector<Point> out;
    out.reserve(points.size() + 2);

    auto inside = [clipX](Point p) { return p.x <= clipX; };
    auto intersect = [clipX](Point a, Point b) {
        const float denom = b.x - a.x;
        const float t = (std::abs(denom) > 1e-4f) ? std::clamp((clipX - a.x) / denom, 0.f, 1.f) : 0.f;
        return Point{clipX, a.y + (b.y - a.y) * t};
    };

    Point prev = points.back();
    bool prevInside = inside(prev);
    for (Point curr : points) {
        const bool currInside = inside(curr);
        if (currInside != prevInside)
            out.push_back(intersect(prev, curr));
        if (currInside)
            out.push_back(curr);
        prev = curr;
        prevInside = currInside;
    }

    return out;
}

HudColor colorAtX(float x, float leftX, float width, HudColor leftColor, HudColor rightColor)
{
    const float t = std::clamp((x - leftX) / std::max(1.f, width), 0.f, 1.f);
    return voidfall::lerpColor(leftColor, rightColor, t);
}

void drawFilledShape(HudContext& ctx,
                     float x,
                     float y,
                     float w,
                     float h,
                     float chamfer,
                     float cornerCut,
                     float fill01,
                     HudColor leftColor,
                     HudColor rightColor)
{
    const float fill = std::clamp(fill01, 0.f, 1.f);
    if (fill <= 0.f)
        return;

    const auto shape = barShape(x, y, w, h, chamfer, cornerCut);
    const std::vector<Point> poly = clipLeftToRight(shape, x + w * fill);
    if (poly.size() < 3)
        return;

    const Point origin = poly.front();
    const HudColor originColor = colorAtX(origin.x, x, w, leftColor, rightColor);
    for (std::size_t i = 1; i + 1 < poly.size(); ++i) {
        const Point a = poly[i];
        const Point b = poly[i + 1];
        ctx.triangleColors(origin.x,
                           origin.y,
                           originColor,
                           a.x,
                           a.y,
                           colorAtX(a.x, x, w, leftColor, rightColor),
                           b.x,
                           b.y,
                           colorAtX(b.x, x, w, leftColor, rightColor));
    }
}

void drawShapeOutline(
    HudContext& ctx, float x, float y, float w, float h, float chamfer, float cornerCut, float thickness, HudColor color)
{
    const auto shape = barShape(x, y, w, h, chamfer, cornerCut);
    const float points[] = {
        shape[0].x,
        shape[0].y,
        shape[1].x,
        shape[1].y,
        shape[2].x,
        shape[2].y,
        shape[3].x,
        shape[3].y,
        shape[4].x,
        shape[4].y,
        shape[5].x,
        shape[5].y,
        shape[0].x,
        shape[0].y,
    };
    ctx.polyline(points, 7, thickness, color);
    const float radius = thickness * 0.5f;
    for (const Point p : shape)
        ctx.roundedRect(p.x - radius, p.y - radius, radius * 2.f, radius * 2.f, radius, color);
}
} // namespace

HealthArmorBar::HealthArmorBar()
{
    anchor = HudAnchor::TopCenter;
    offsetX = -250.f;
    offsetY = 98.f;
    width = panelWidth;
    height = barHeight;
}

void HealthArmorBar::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;

    maxHealth_ = std::max(1, state.maxHealth);
    maxArmor_ = std::max(1, state.maxArmor);
    healthFill_ = std::clamp(static_cast<float>(state.health) / static_cast<float>(maxHealth_), 0.f, 1.f);
    armorFill_ = std::clamp(static_cast<float>(state.armor) / static_cast<float>(maxArmor_), 0.f, 1.f);
}

void HealthArmorBar::draw(HudContext& ctx, float x, float y)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float w = panelWidth * s;
    const float h = barHeight * s;
    const float chamfer = chamferSize * s;
    const float cornerCut = cornerCutSize * s;
    const float outline = std::max(1.f, outlineThickness * s);
    const float topY = y - h;

    drawFilledShape(ctx, x, topY, w, h, chamfer, cornerCut, healthFill_, k_red, k_redBright);
    drawFilledShape(ctx, x, topY, w, h, chamfer, cornerCut, armorFill_, k_cyanDim, k_cyan);
    drawShapeOutline(ctx, x, topY, w, h, chamfer, cornerCut, outline, k_lineBright);
}
