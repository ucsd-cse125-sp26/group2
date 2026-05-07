/// @file HitMarkerWidget.hpp
/// @brief Voidfall hit-marker — distinct shape per confirm type.
///
/// Four variants from the prototype:
///   - SHIELD:   short cyan diagonal corner ticks (thin).
///   - HP:       longer amber diagonal corner ticks (thicker).
///   - HEADSHOT: red triangles pointing inward at the four cardinals.
///   - KILL:     bright white ring + thick diagonal X.

#pragma once

#include "hud/HudWidget.hpp"

struct HitMarkerWidget : HudWidget
{
    enum class Kind
    {
        None,
        Shield,
        Hp,
        Headshot,
        Kill,
    };

    float armLength = 9.f;
    float armThickness = 2.5f;
    float armGap = 7.f;
    float fadeDuration = 0.45f;
    float killFadeDuration = 0.6f;
    float killRingRadius = 14.f;
    float headshotTriangleSize = 6.f;

    HitMarkerWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    Kind kind_ = Kind::None;
    float alpha_ = 0.f;
    float scale_ = 1.f;
};
