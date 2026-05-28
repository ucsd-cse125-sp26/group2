/// @file ShotgunPelletWidget.cpp
/// @brief 9-pellet star-pattern readout for shotgun blasts.

#include "ShotgunPelletWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudTween.hpp"

#include <array>

namespace {

// Pentagon-vertex unit offsets (tangent plane: x=right, y=up). 72° spacing
// starting from straight-up. Matches the server's `k_offsets` in
// WeaponSystem.cpp exactly so the on-screen pellet position corresponds to
// the actual ray that was fired.
constexpr std::array<std::pair<float, float>, 5> k_pelletPositions{{
    { 0.0000f,  1.0000f}, // top         (90°)
    {-0.9511f,  0.3090f}, // upper-left  (162°)
    {-0.5878f, -0.8090f}, // lower-left  (234°)
    { 0.5878f, -0.8090f}, // lower-right (306°)
    { 0.9511f,  0.3090f}, // upper-right (18°)
}};

constexpr HudColor k_pelletMiss{0.92f, 0.92f, 0.92f, 1.0f}; // white
constexpr HudColor k_pelletBody{0.95f, 0.20f, 0.20f, 1.0f}; // red
constexpr HudColor k_pelletHead{1.00f, 0.85f, 0.15f, 1.0f}; // gold
constexpr HudColor k_pelletBg{0.05f, 0.05f, 0.05f, 0.45f};  // subtle backdrop disc

} // namespace

ShotgunPelletWidget::ShotgunPelletWidget()
{
    // Bottom-right area, just inboard from the ammo counter. Tweak as needed.
    anchor = HudAnchor::BottomRight;
    offsetX = -180.f;
    offsetY = -180.f;
    width = 64.f;
    height = 64.f;
    visible = true;
}

void ShotgunPelletWidget::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    // Detect a freshly-staged blast (Game.cpp resets secondsSinceFire to 0).
    if (state.latestShotgunBlast.valid) {
        const bool isNew = !primed_ || state.latestShotgunBlast.secondsSinceFire < age_;
        if (isNew) {
            latest_ = state.latestShotgunBlast;
            age_ = 0.f;
            alpha_ = 1.f;
            primed_ = true;
        }
    }

    age_ += dt;

    // Hold full opacity for `holdDuration`, then linear fade over `fadeDuration`.
    if (age_ < holdDuration) {
        alpha_ = 1.f;
    } else if (age_ < holdDuration + fadeDuration) {
        alpha_ = 1.f - (age_ - holdDuration) / fadeDuration;
    } else {
        alpha_ = 0.f;
    }
}

void ShotgunPelletWidget::draw(HudContext& ctx, float cx, float cy)
{
    if (!primed_ || alpha_ < 0.01f)
        return;

    const float s = uiScale_;
    const float r = radius * s;
    const float dr = dotRadius * s;

    auto withFade = [&](HudColor c) -> HudColor { return HudColor{c.r, c.g, c.b, c.a * alpha_}; };

    // Faint backdrop disc so dots are readable against bright HUD elements.
    {
        const float bg = (r + dr) * 2.f;
        const HudColor bgFade = withFade(k_pelletBg);
        ctx.roundedRect(cx - bg * 0.5f, cy - bg * 0.5f, bg, bg, bg * 0.5f, bgFade);
    }

    for (std::size_t i = 0; i < latest_.pellets.size(); ++i) {
        const auto [ox, oy] = k_pelletPositions[i];
        const float px = cx + ox * r;
        // Screen Y grows downward; pattern Y grows upward (up = positive). Flip.
        const float py = cy - oy * r;

        HudColor col;
        switch (latest_.pellets[i].result) {
        case HudShotgunPellet::Result::Head:
            col = k_pelletHead;
            break;
        case HudShotgunPellet::Result::Body:
            col = k_pelletBody;
            break;
        case HudShotgunPellet::Result::Miss:
        default:
            col = k_pelletMiss;
            break;
        }
        col = withFade(col);

        // Filled circle via roundedRect with radius = half-side.
        const float d = dr * 2.f;
        ctx.roundedRect(px - dr, py - dr, d, d, dr, col);
    }
}
