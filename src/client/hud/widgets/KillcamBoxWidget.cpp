/// @file KillcamBoxWidget.cpp
/// @brief Red killer-target box for the killcam death screen.

#include "KillcamBoxWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <algorithm>

namespace
{
// Literal red — the Voidfall palette remaps its "k_red" token to cyan, but the
// killcam box must read as unambiguously hostile.
constexpr HudColor k_killRed{0.95f, 0.16f, 0.16f, 1.0f};

/// @brief Project a world point to screen pixels. Returns false if behind cam.
/// Mirrors EnemyWorldHealthBar's convention (NDC Y up → screen Y down flip).
bool projectToScreen(const glm::mat4& vp, const glm::vec3& world, float screenW, float screenH, float& outX,
                     float& outY)
{
    const glm::vec4 clip = vp * glm::vec4(world, 1.f);
    if (clip.w <= 0.0001f)
        return false;
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    outX = (ndcX * 0.5f + 0.5f) * screenW;
    outY = (1.0f - (ndcY * 0.5f + 0.5f)) * screenH;
    return true;
}
} // namespace

KillcamBoxWidget::KillcamBoxWidget()
{
    anchor = HudAnchor::TopLeft;
    visible = true;
}

void KillcamBoxWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    box_ = state.killerBox;
    viewProj_ = state.viewProj;
    screenW_ = state.screenW;
    screenH_ = state.screenH;
    visible = !state.isAlive && box_.valid;
}

void KillcamBoxWidget::draw(HudContext& ctx, float /*drawX*/, float /*drawY*/)
{
    using namespace voidfall;
    if (!box_.valid)
        return;

    // Project all 8 AABB corners and take the screen-space bounding rect of
    // those in front of the camera.
    const glm::vec3 c = box_.center;
    const glm::vec3 h = box_.halfExtents;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    int projected = 0;
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner{
            c.x + ((i & 1) ? h.x : -h.x),
            c.y + ((i & 2) ? h.y : -h.y),
            c.z + ((i & 4) ? h.z : -h.z),
        };
        float sx = 0.f, sy = 0.f;
        if (!projectToScreen(viewProj_, corner, screenW_, screenH_, sx, sy))
            continue;
        minX = std::min(minX, sx);
        minY = std::min(minY, sy);
        maxX = std::max(maxX, sx);
        maxY = std::max(maxY, sy);
        ++projected;
    }
    if (projected == 0)
        return; // Entirely behind the camera.

    const float s = uiScale_;
    // Enforce a minimum on-screen size so a distant killer is still framed.
    const float minSz = minBoxPx * s;
    if (maxX - minX < minSz) {
        const float cx = (minX + maxX) * 0.5f;
        minX = cx - minSz * 0.5f;
        maxX = cx + minSz * 0.5f;
    }
    if (maxY - minY < minSz) {
        const float cy = (minY + maxY) * 0.5f;
        minY = cy - minSz * 0.5f;
        maxY = cy + minSz * 0.5f;
    }

    const float x = minX;
    const float y = minY;
    const float w = maxX - minX;
    const float hgt = maxY - minY;
    const float t = std::max(1.f, lineThickness * s);

    // Faint red wash + bright outline + corner brackets for a target-locked look.
    ctx.rect(x, y, w, hgt, withAlpha(k_killRed, 0.10f));
    ctx.rectOutline(x, y, w, hgt, t, k_killRed);
    drawCornerBrackets(ctx, x, y, w, hgt, std::min(w, hgt) * 0.28f, t, 0.f, k_killRed);

    // Label above the box.
    const float fs = 16.f * s;
    ctx.text("KILLER", (x + maxX) * 0.5f, y - fs - 4.f * s, fs, k_killRed, HudAlign::Center, /*outlined=*/true);
}
