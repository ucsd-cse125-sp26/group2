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
    const float bottomCut = std::clamp(cornerCut, 0.f, std::min(cut, h * 0.5f));
    return {{
        {x + cut, y},
        {x + w - cut, y},
        {x + w, y + h - bottomCut},
        {x + w - bottomCut, y + h},
        {x + bottomCut, y + h},
        {x, y + h - bottomCut},
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

void drawGradientFilledShape(HudContext& ctx,
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

void drawFillEdge(HudContext& ctx,
                  float x,
                  float y,
                  float w,
                  float h,
                  float fill01,
                  float thickness,
                  HudColor color)
{
    const float fill = std::clamp(fill01, 0.f, 1.f);
    if (fill <= 0.01f)
        return;

    const float fillX = x + w * fill;
    const float edgeH = h * 0.82f;
    const float edgeY = y + (h - edgeH) * 0.5f;
    ctx.rect(fillX - thickness * 0.5f,
             edgeY,
             thickness,
             edgeH,
             voidfall::withAlpha(color, fill < 0.995f ? 0.90f : 0.55f));
    ctx.rect(fillX - thickness * 1.5f,
             edgeY,
             thickness * 3.f,
             edgeH,
             voidfall::withAlpha(color, fill < 0.995f ? 0.22f : 0.12f));
}

void drawShapeOutline(HudContext& ctx,
                      float x,
                      float y,
                      float w,
                      float h,
                      float chamfer,
                      float cornerCut,
                      float thickness,
                      HudColor color)
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

void drawGlassFrame(HudContext& ctx, float x, float y, float w, float h, float chamfer, float cornerCut, float outline)
{
    using namespace voidfall;

    drawShapeOutline(ctx, x, y, w, h, chamfer, cornerCut, outline * 2.6f, withAlpha(k_chromaCyan, 0.12f));
    drawShapeOutline(ctx, x, y, w, h, chamfer, cornerCut, outline * 1.7f, withAlpha(k_secondary, 0.48f));
    drawShapeOutline(ctx, x, y, w, h, chamfer, cornerCut, outline, k_lineBright);
    drawShapeOutline(ctx, x, y, w, h, chamfer, cornerCut, std::max(1.f, outline * 0.36f), k_cyan);

    const float insetX = outline * 1.25f;
    const float insetY = outline * 0.85f;
    if (w > insetX * 2.f && h > insetY * 2.f) {
        drawShapeOutline(ctx,
                         x + insetX,
                         y + insetY,
                         w - insetX * 2.f,
                         h - insetY * 2.f,
                         std::max(0.f, chamfer - insetX),
                         std::max(0.f, cornerCut - insetY),
                         std::max(1.f, outline * 0.30f),
                         withAlpha(k_textBright, 0.72f));
    }
}
} // namespace

HealthArmorBar::HealthArmorBar()
{
    anchor = HudAnchor::TopCenter;
    offsetX = -375.f;
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
    const float inset = outline * 1.5f;
    const float innerX = x + inset;
    const float innerY = topY + inset;
    const float innerW = w - inset * 2.f;
    const float innerH = h - inset * 2.f;
    const float innerChamfer = std::max(0.f, chamfer - inset);
    const float innerCornerCut = std::max(0.f, cornerCut - inset);

    drawGradientFilledShape(ctx,
                            x,
                            topY,
                            w,
                            h,
                            chamfer,
                            cornerCut,
                            1.f,
                            withAlpha(k_quaternary, 0.88f),
                            withAlpha(k_secondary, 0.56f));
    drawGlassFrame(ctx, x, topY, w, h, chamfer, cornerCut, outline);

    drawGradientFilledShape(ctx,
                            innerX,
                            innerY,
                            innerW,
                            innerH,
                            innerChamfer,
                            innerCornerCut,
                            healthFill_,
                            withAlpha(k_secondary, 0.80f),
                            k_healthBright);
    drawFillEdge(ctx, innerX, innerY, innerW, innerH, healthFill_, std::max(2.f, outline * 0.55f), k_textBright);

    if (armorFill_ > 0.01f) {
        drawGradientFilledShape(ctx,
                                innerX,
                                innerY,
                                innerW,
                                innerH,
                                innerChamfer,
                                innerCornerCut,
                                armorFill_,
                                withAlpha(k_primary, 0.80f),
                                k_cyan);
        drawFillEdge(ctx, innerX, innerY, innerW, innerH, armorFill_, std::max(2.f, outline * 0.55f), k_cyan);
    }
}
