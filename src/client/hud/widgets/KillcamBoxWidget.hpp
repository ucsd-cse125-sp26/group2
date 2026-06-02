/// @file KillcamBoxWidget.hpp
/// @brief Red bounding box framing the killer on the killcam death screen.

#pragma once

#include "hud/HudWidget.hpp"

#include <glm/glm.hpp>

/// @brief While the local player is dead and the killcam is tracking the
/// killer, projects the killer's world AABB to screen space and draws a red
/// target box (outline + corner brackets + label) around them.
struct KillcamBoxWidget : HudWidget
{
    KillcamBoxWidget();

    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

    float lineThickness = 2.5f; ///< Box outline thickness (logical px at 1080p).
    float minBoxPx = 24.f;      ///< Minimum on-screen box size so distant killers stay visible.

private:
    HudKillerBox box_{};
    glm::mat4 viewProj_{1.f};
    float screenW_ = 1280.f;
    float screenH_ = 720.f;
};
