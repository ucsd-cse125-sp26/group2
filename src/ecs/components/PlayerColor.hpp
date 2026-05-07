/// @file PlayerColor.hpp
/// @brief Per-player tint color component (replicated to clients).

#pragma once

#include <glm/vec3.hpp>

/// @brief Replicated tint assigned to a player on connect.
///
/// Server picks the least-used slot from `player_colors::k_palette` in
/// ServerGame::initNewPlayerEntity and attaches this component. Replicated
/// via the `Synced` tuple so clients pick it up automatically; the renderer
/// reads `rgb` when filling SkinnedInstance.tint.
struct PlayerColor
{
    glm::vec3 rgb{1.0f}; ///< Tint color in linear-ish RGB.
    int paletteIdx{-1};  ///< Slot in player_colors::k_palette; -1 = unassigned.
};
