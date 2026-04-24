/// @file AnimationTesterUI.hpp
/// @brief ImGui panel for driving the animation state machine in development.

#pragma once

#include <entt/entt.hpp>

/// @brief Persistent UI state for the Animation Tester panel.
///
/// Kept alive across frames so combo boxes, checkboxes, and sliders remember
/// their selection.  Owned by `Game` (a plain field on the class).
struct AnimationTesterState
{
    bool show = false;             ///< Window visibility toggle.
    int selectedClip = 0;          ///< Integer value of a `ClipId`.
    int targetEntityRaw = -1;      ///< `entt::entity` raw value, or -1 for "local player".
    bool showLocalBody = false;    ///< Render the local player's own body (third-person debug).
    float playbackSpeedMul = 1.0f; ///< Multiplier applied in debug-override playback.
};

/// @brief Build the Animation Tester ImGui window.
/// @param state     Persistent UI state.
/// @param registry  ECS registry (queried for `AnimatedCharacter` entities).
/// @param rigScale  Renderable scale tunable (read/write).
/// @param rigVerticalOffset  Renderable Y translation tunable (read/write).
void buildAnimationTesterUI(AnimationTesterState& state,
                            entt::registry& registry,
                            float& rigScale,
                            float& rigVerticalOffset);
