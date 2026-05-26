/// @file HudWidget.hpp
/// @brief Base struct for all HUD widgets.

#pragma once

#include "HudTypes.hpp"

class HudContext;
class HudTweenPool;

/// @brief Base class for a retained HUD element.
///
/// Widgets own their state and animation.  Their draw() method uses
/// HudContext's immediate-mode API to emit geometry.
struct HudWidget
{
    bool visible = true;

    HudAnchor anchor = HudAnchor::TopLeft;
    float offsetX = 0.f, offsetY = 0.f;
    float width = 0.f, height = 0.f;
    HudColor tint{1.f, 1.f, 1.f, 1.f};
    float uiScale_ = 1.f; ///< Set by Hud each frame from a 1920x1080 reference canvas.

    virtual ~HudWidget() = default;

    /// @brief Called each frame before draw().  Update animation, consume events.
    virtual void update(float dt, const HudGameState& state, HudTweenPool& tweens) = 0;

    /// @brief Emit geometry into the draw context.
    /// @param ctx  Immediate-mode draw API.
    /// @param drawX Resolved pixel X (anchor + offset already applied).
    /// @param drawY Resolved pixel Y.
    virtual void draw(HudContext& ctx, float drawX, float drawY) = 0;
};
