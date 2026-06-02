/// @file KillcamBoxWidget.hpp
/// @brief Floating killer nickname on the killcam death screen.

#pragma once

#include "hud/HudWidget.hpp"

#include <glm/glm.hpp>

/// @brief While the local player is dead and the killcam is tracking the
/// killer, floats the killer's nickname above them in red. (The wallhack
/// silhouette itself is drawn by the 3D renderer's chams pass.)
struct KillcamBoxWidget : HudWidget
{
    KillcamBoxWidget();

    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    HudKillerBox box_{};
    glm::mat4 viewProj_{1.f};
    float screenW_ = 1280.f;
    float screenH_ = 720.f;
};
