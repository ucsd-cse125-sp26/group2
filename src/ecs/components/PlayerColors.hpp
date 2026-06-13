/// @file PlayerColors.hpp
/// @brief Palette + tunables for the per-player color tint feature.
///
/// On player connect, the server picks the least-used palette slot and
/// attaches a PlayerColor component. The client renderer reads it when
/// filling SkinnedInstance.tint and the skinned PBR shader blends the
/// albedo toward that color so players are easier to tell apart.
///
/// The single `k_enabled` toggle disables the whole feature: server skips
/// assignment, client tint defaults to white-with-blend-zero (no-op).

#pragma once

#include <array>
#include <glm/vec3.hpp>

namespace player_colors
{

/// @brief Master toggle. Flip to false to disable per-player tinting entirely.
inline constexpr bool k_enabled = true;

/// @brief Mix factor in the shader: 0 = original albedo, 1 = full assigned player color.
inline constexpr float k_blendFactor = 1.0f;

/// @brief Curated palette of visually distinct hues.
///
/// Sized larger than typical match cap so reservation rarely wraps. Picked
/// for separation in a busy scene under HDR + tonemap; saturated, mid-bright.
inline constexpr std::array<glm::vec3, 10> k_palette = {{
    {0.95f, 0.20f, 0.20f}, // 0 red
    {0.25f, 0.45f, 0.95f}, // 1 blue
    {0.30f, 0.85f, 0.25f}, // 2 green
    {0.95f, 0.55f, 0.10f}, // 3 orange
    {0.65f, 0.30f, 0.95f}, // 4 purple
    {0.95f, 0.90f, 0.20f}, // 5 yellow
    {0.20f, 0.85f, 0.85f}, // 6 cyan
    {0.95f, 0.30f, 0.85f}, // 7 magenta
    {0.95f, 0.55f, 0.75f}, // 8 pink
    {0.55f, 0.95f, 0.55f}, // 9 mint
}};

inline constexpr int k_paletteSize = static_cast<int>(k_palette.size());

} // namespace player_colors
