/// @file ScreenDecalWidget.hpp
/// @brief Top and bottom screen decal HUD widgets.

#pragma once

#include "hud/HudWidget.hpp"

struct ScreenDecalWidget : HudWidget
{
    float decalWidth = 1920.f;
    float decalHeight = 165.65f;
    float decalScale = 1.f;
    float decalOffsetX = 0.f;
    float decalOffsetY = 0.f;
    float decalStretchX = 1.f;
    float decalStretchY = 1.f;
    float decalRotationDeg = 0.f;

    [[nodiscard]] bool isTopDecal() const { return top_; }

    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

protected:
    explicit ScreenDecalWidget(bool top);

private:
    bool top_ = true;
};

struct TopDecalWidget final : ScreenDecalWidget
{
    TopDecalWidget();
};

struct BottomDecalWidget final : ScreenDecalWidget
{
    BottomDecalWidget();
};
