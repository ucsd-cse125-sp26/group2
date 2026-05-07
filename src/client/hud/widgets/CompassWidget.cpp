/// @file CompassWidget.cpp
/// @brief Voidfall compass strip implementation.

#include "CompassWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <numbers>

namespace
{
constexpr const char* k_cardinals[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
}

CompassWidget::CompassWidget()
{
    anchor = HudAnchor::TopCenter;
    offsetX = 0.f;
    offsetY = 76.f; // sits below the match header
}

void CompassWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    // Yaw radians → compass degrees (0 = +Z = north, CW positive).
    float deg = state.localPlayerYaw * (180.f / std::numbers::pi_v<float>);
    deg = std::fmod(deg, 360.f);
    if (deg < 0.f)
        deg += 360.f;
    headingDeg_ = deg;
}

void CompassWidget::draw(HudContext& ctx, float cx, float y)
{
    using namespace voidfall;

    const float s = uiScale_;
    const float w = stripWidth * s;
    const float h = stripHeight * s;
    const float x = cx - w * 0.5f;
    const float halfFov = fovDeg * 0.5f;

    drawPanel(ctx, x, y, w, h, HudColor{0.10f, 0.09f, 0.08f, 0.85f}, k_lineDim, 1.f);

    // Ticks every 5° across the visible FOV.
    for (int d = -static_cast<int>(halfFov); d <= static_cast<int>(halfFov); d += 5) {
        const float relX = (static_cast<float>(d) / halfFov) * (w * 0.5f);
        const float tx = cx + relX;
        const float actual = std::fmod(headingDeg_ + static_cast<float>(d) + 360.f, 360.f);

        const bool isCardinal = std::fmod(actual + 1.5f, 90.f) < 3.f;
        const bool isMajor = std::fmod(actual + 2.5f, 45.f) < 5.f;
        const float tickH = isCardinal ? 8.f * s : (isMajor ? 5.f * s : 3.f * s);
        const HudColor tickC = isCardinal ? k_amber : k_lineBright;
        ctx.rect(tx - 0.5f * s, y, 1.f * s, tickH, tickC);

        if (isCardinal) {
            const int idx = static_cast<int>(std::round(actual / 45.f)) % 8;
            ctx.text(k_cardinals[idx], tx, y + 9.f * s, labelFontSize * s, k_amber, HudAlign::Center);
        }
    }

    // Center pointer chevron (above strip).
    {
        const float ts = 5.f * s;
        const float tx = cx;
        const float ty = y - 1.f * s;
        ctx.rotatedRect(tx, ty, ts, ts, 45.f, k_amber);
    }

    // Bearing readout below strip.
    char buf[16];
    SDL_snprintf(buf, sizeof(buf), "%03d", static_cast<int>(headingDeg_));
    char buf2[16];
    SDL_snprintf(
        buf2, sizeof(buf2), "%s%c", buf, '\xb0'); // 0xB0 ≈ degree sign in extended ASCII; SDF font likely lacks it.
    // Most ASCII-only SDF fonts won't render `°`; fall back to a plain "DEG"
    // suffix for portability across the project's existing atlas.
    SDL_snprintf(buf2, sizeof(buf2), "%s DEG", buf);
    ctx.text(buf2, cx, y + h + 2.f * s, bearingFontSize * s, k_amber, HudAlign::Center);
}
