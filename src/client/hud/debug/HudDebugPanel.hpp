/// @file HudDebugPanel.hpp
/// @brief ImGui panel for live-tweaking HUD widget layout and parameters.

#pragma once

class Hud;

/// @brief Builds an ImGui window exposing HUD widget layout constants.
namespace HudDebugPanel
{

/// @brief Build the HUD tweaker panel.
/// @param hud Reference to the active HUD system.
/// @param open Visibility toggle (linked to DebugUI's panel toggling).
void build(Hud& hud, bool* open);

} // namespace HudDebugPanel
