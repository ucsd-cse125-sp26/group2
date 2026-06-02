/// @file KillcamBoxWidget.cpp
/// @brief Floating killer nickname for the killcam death screen.
///
/// The killer's wallhack-style red silhouette is drawn by the 3D renderer
/// (chams pass); this widget only floats the killer's nickname above them.

#include "KillcamBoxWidget.hpp"

#include "hud/HudContext.hpp"

namespace
{
// Literal red — the Voidfall palette remaps its "k_red" token to cyan, but the
// killcam marker must read as unambiguously hostile.
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
    visible = !state.isAlive && box_.valid && !box_.name.empty();
}

void KillcamBoxWidget::draw(HudContext& ctx, float /*drawX*/, float /*drawY*/)
{
    if (!box_.valid || box_.name.empty())
        return;

    // Float the nickname just above the killer's head (AABB top-center).
    const glm::vec3 headWorld = box_.center + glm::vec3{0.f, box_.halfExtents.y + 12.f, 0.f};
    float sx = 0.f, sy = 0.f;
    if (!projectToScreen(viewProj_, headWorld, screenW_, screenH_, sx, sy))
        return; // Behind the camera.

    const float s = uiScale_;
    const float fs = 18.f * s;
    ctx.text(box_.name.c_str(), sx, sy - fs, fs, k_killRed, HudAlign::Center, /*outlined=*/true);
}
