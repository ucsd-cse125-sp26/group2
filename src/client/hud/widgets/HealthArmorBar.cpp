/// @file HealthArmorBar.cpp
/// @brief SVG-framed health and shield bar.

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

struct SvgTransform
{
    float x = 0.f;
    float y = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
};

constexpr float k_svgWidth = 835.22501f;
constexpr float k_svgHeight = 62.730918f;
constexpr float k_groupTx = 3.6703754f;
constexpr float k_groupTy = -15.663858f;

// Coordinates copied from assets/hud_icons/HealthFrameBack.svg and
// HealthFrameFront.svg. The two files share the same frame path; the front
// layer exposes only the outline/details while the back layer exposes only the
// filled silhouette.
constexpr std::array<Point, 6> k_framePath{{
    {36.819013f, 17.913858f},
    {790.2451f, 17.913858f},
    {828.f, 46.915786f},
    {794.33617f, 76.144778f},
    {38.18268f, 76.144778f},
    {0.f, 45.705888f},
}};

Point transformPoint(Point p, const SvgTransform& t)
{
    return Point{t.x + (p.x + k_groupTx) * t.scaleX, t.y + (p.y + k_groupTy) * t.scaleY};
}

std::vector<Point> transformedFrame(const SvgTransform& t)
{
    std::vector<Point> out;
    out.reserve(k_framePath.size());
    for (const Point p : k_framePath)
        out.push_back(transformPoint(p, t));
    return out;
}

std::vector<Point> clipVertical(const std::vector<Point>& points, float clipX, bool keepLeft)
{
    std::vector<Point> out;
    out.reserve(points.size() + 2);
    if (points.empty())
        return out;

    const auto inside = [clipX, keepLeft](Point p) { return keepLeft ? p.x <= clipX : p.x >= clipX; };
    const auto intersect = [clipX](Point a, Point b) {
        const float denom = b.x - a.x;
        const float u = (std::abs(denom) > 1e-4f) ? std::clamp((clipX - a.x) / denom, 0.f, 1.f) : 0.f;
        return Point{clipX, a.y + (b.y - a.y) * u};
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

std::vector<Point> clipSegment(const std::vector<Point>& points, float leftX, float rightX)
{
    if (rightX <= leftX)
        return {};
    std::vector<Point> clipped = clipVertical(points, rightX, true);
    return clipVertical(clipped, leftX, false);
}

void drawPolygon(HudContext& ctx, const std::vector<Point>& points, HudColor color)
{
    if (points.size() < 3)
        return;

    const Point origin = points.front();
    for (std::size_t i = 1; i + 1 < points.size(); ++i)
        ctx.triangle(origin.x, origin.y, points[i].x, points[i].y, points[i + 1].x, points[i + 1].y, color);
}

void drawFrameFill(HudContext& ctx, const std::vector<Point>& frame, float leftX, float rightX, HudColor color)
{
    drawPolygon(ctx, clipSegment(frame, leftX, rightX), color);
}

void drawPolyline(HudContext& ctx, const std::vector<Point>& points, float thickness, HudColor color)
{
    if (points.size() < 2)
        return;

    std::vector<float> packed;
    packed.reserve(points.size() * 2);
    for (const Point p : points) {
        packed.push_back(p.x);
        packed.push_back(p.y);
    }
    ctx.polyline(packed.data(), static_cast<int>(points.size()), thickness, color);
}

Point cubic(Point a, Point b, Point c, Point d, float u)
{
    const float v = 1.f - u;
    const float aa = v * v * v;
    const float bb = 3.f * v * v * u;
    const float cc = 3.f * v * u * u;
    const float dd = u * u * u;
    return Point{
        aa * a.x + bb * b.x + cc * c.x + dd * d.x,
        aa * a.y + bb * b.y + cc * c.y + dd * d.y,
    };
}

std::vector<Point> transformedCubic(Point a, Point b, Point c, Point d, const SvgTransform& t, int steps = 8)
{
    std::vector<Point> out;
    out.reserve(static_cast<std::size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(steps);
        out.push_back(transformPoint(cubic(a, b, c, d, u), t));
    }
    return out;
}

void drawFrontCaps(HudContext& ctx, const SvgTransform& t, HudColor color)
{
    std::vector<Point> leftCap = transformedCubic({7.2575816f, 51.491567f},
                                                  {6.9687296f, 51.734241f},
                                                  {52.657662f, 17.913858f},
                                                  {53.110096f, 17.913858f},
                                                  t);
    leftCap.push_back(transformPoint({46.620768f, 17.913858f}, t));
    drawPolygon(ctx, leftCap, color);

    std::vector<Point> rightCap = transformedCubic({816.99477f, 41.234041f},
                                                   {782.7473f, 78.013701f},
                                                   {787.97606f, 76.144778f},
                                                   {787.41052f, 76.144778f},
                                                   t);
    rightCap.push_back(transformPoint({777.49225f, 76.144778f}, t));

    const std::vector<Point> returnCurve = transformedCubic({777.49225f, 76.144778f},
                                                            {777.49225f, 76.144778f},
                                                            {817.46391f, 40.695859f},
                                                            {816.99477f, 41.234041f},
                                                            t);
    rightCap.insert(rightCap.end(), returnCurve.begin() + 1, returnCurve.end());
    drawPolygon(ctx, rightCap, color);
}

void drawFrontFrame(HudContext& ctx, const SvgTransform& t)
{
    using namespace voidfall;

    const std::vector<Point> frame = transformedFrame(t);
    std::vector<Point> closedFrame = frame;
    closedFrame.push_back(frame.front());

    const float strokeScale = std::max(0.2f, (std::abs(t.scaleX) + std::abs(t.scaleY)) * 0.5f);
    drawPolyline(ctx, closedFrame, std::max(1.f, 4.5f * strokeScale), k_lineBright);

    const std::vector<Point> leftInner = transformedCubic({7.2575816f, 51.491567f},
                                                          {7.6033706f, 51.767227f},
                                                          {46.168334f, 20.301307f},
                                                          {46.620768f, 17.913858f},
                                                          t);
    drawPolyline(ctx, leftInner, std::max(1.f, 2.f * strokeScale), k_lineBright);

    const std::vector<Point> rightInner = transformedCubic({777.49225f, 76.144778f},
                                                           {777.81217f, 74.449006f},
                                                           {820.71721f, 41.321415f},
                                                           {817.86071f, 39.127159f},
                                                           t);
    drawPolyline(ctx, rightInner, std::max(1.f, 2.f * strokeScale), k_lineBright);

    drawFrontCaps(ctx, t, k_lineBright);
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
    const float safeScale = std::max(0.01f, svgScale);
    const float safeStretchX = std::max(0.01f, svgStretchX);
    const float safeStretchY = std::max(0.01f, svgStretchY);
    const float frameW = panelWidth * s * safeScale * safeStretchX;
    const float frameH = barHeight * s * safeScale * safeStretchY;
    const float frameX = x + svgOffsetX * s;
    const float frameY = y - frameH + svgOffsetY * s;

    width = panelWidth * safeScale * safeStretchX;
    height = barHeight * safeScale * safeStretchY;

    const SvgTransform t{
        frameX,
        frameY,
        frameW / k_svgWidth,
        frameH / k_svgHeight,
    };

    const std::vector<Point> frame = transformedFrame(t);
    const float leftX = frameX;

    drawPolygon(ctx, frame, HudColor{k_secondary.r, k_secondary.g, k_secondary.b, 0.22f});

    const float totalMax = static_cast<float>(maxHealth_ + maxArmor_);
    const float healthSegment = (static_cast<float>(maxHealth_) * healthFill_) / totalMax;
    const float shieldSegment = (static_cast<float>(maxArmor_) * armorFill_) / totalMax;
    const float healthRight = leftX + frameW * std::clamp(healthSegment, 0.f, 1.f);
    const float shieldRight = leftX + frameW * std::clamp(healthSegment + shieldSegment, 0.f, 1.f);

    drawFrameFill(ctx, frame, leftX, healthRight, k_health);
    drawFrameFill(ctx, frame, healthRight, shieldRight, k_cyan);
    drawFrontFrame(ctx, t);
}
