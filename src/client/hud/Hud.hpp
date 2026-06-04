/// @file Hud.hpp
/// @brief Top-level HUD system — owns widgets, renderer, context, tweens.

#pragma once

#include "HudContext.hpp"
#include "HudRenderer.hpp"
#include "HudSvgAtlas.hpp"
#include "HudTween.hpp"
#include "HudTypes.hpp"
#include "HudWidget.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <vector>

class SdfAtlas;

/// @brief Top-level HUD system.  Owns all layers and widgets.
///
/// Game calls update() + render() each frame.  The renderer reads
/// getOutputTexture() and blits it after tone mapping.
class Hud
{
public:
    /// @brief Initialise all HUD subsystems and create default widgets.
    bool init(SDL_GPUDevice* device,
              SDL_GPUShaderFormat shaderFormat,
              const SdfAtlas& sdfAtlas,
              uint32_t screenW,
              uint32_t screenH);

    /// @brief Release all resources.
    void quit();

    /// @brief Notify of window resize.
    void resize(uint32_t newW, uint32_t newH);

    /// @brief Forward an SDL event to interactive widgets (buy menu, scoreboard).
    void processEvent(const SDL_Event* event, const InputBindings* bindings);

    /// @brief Tick widget animations and consume game events.
    void update(float dt, const HudGameState& state);

    /// @brief Draw all widgets and flush to the GPU offscreen target.
    void render();

    /// @brief The offscreen texture to blit.
    [[nodiscard]] SDL_GPUTexture* getOutputTexture() const { return renderer_.getOutputTexture(); }

    /// @brief Access all widgets (for debug panel iteration).
    [[nodiscard]] std::vector<std::unique_ptr<HudWidget>>& widgets() { return widgets_; }

    /// @brief Access the tween pool (for debug panel).
    [[nodiscard]] HudTweenPool& tweens() { return tweens_; }

    /// @brief Debug override: render inactive widgets, excluding the railgun scope.
    [[nodiscard]] bool& debugRenderInactiveWidgets() { return debugRenderInactiveWidgets_; }

    /// @brief Debug overlay: draw a screen-space alignment border.
    [[nodiscard]] bool& debugShowAlignmentBorder() { return debugShowAlignmentBorder_; }

    /// @brief Horizontal inset for the alignment border from left and right edges.
    [[nodiscard]] float& debugAlignmentBorderOffsetX() { return debugAlignmentBorderOffsetX_; }

    /// @brief Vertical inset for the alignment border from top and bottom edges.
    [[nodiscard]] float& debugAlignmentBorderOffsetY() { return debugAlignmentBorderOffsetY_; }

private:
    HudRenderer renderer_;
    HudSvgAtlas svgAtlas_;
    HudContext context_;
    HudTweenPool tweens_;
    std::vector<std::unique_ptr<HudWidget>> widgets_;
    bool debugRenderInactiveWidgets_ = false;
    bool debugShowAlignmentBorder_ = false;
    float debugAlignmentBorderOffsetX_ = 80.f;
    float debugAlignmentBorderOffsetY_ = 60.f;

    float screenW_ = 0.f, screenH_ = 0.f;

    /// @brief Resolve anchor + offset to pixel coordinates.
    void resolveAnchor(const HudWidget& w, float& outX, float& outY) const;

    /// @brief Create all default widgets with initial layout.
    void createWidgets();
};
