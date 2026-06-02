/// @file EmoteCatalog.hpp
/// @brief Neutral catalog of selectable emotes (no animation/render deps).
///
/// Shared by the HUD emote wheel, the client input sampler, and the gameplay
/// code that maps an emote index onto an animation clip. Keeping the catalog
/// here (rather than in AnimationLibrary) lets the HUD reference emote names
/// without pulling in the animation backend.

#pragma once

namespace emotes
{

/// @brief Number of emotes shown on the wheel. Must match the index→ClipId
/// mapping in `emoteClipForIndex()` (AnimationLibrary) and the per-emote
/// asset files under assets/emotes/.
inline constexpr int kEmoteCount = 5;

/// @brief Short all-caps label for an emote index (for the wheel widget).
/// Returns "" for out-of-range indices.
inline const char* emoteName(int index)
{
    switch (index) {
    case 0:
        return "FLAIR";
    case 1:
        return "MARASCHINO";
    case 2:
        return "GANGNAM";
    case 3:
        return "HIP HOP";
    case 4:
        return "N. SOUL";
    default:
        return "";
    }
}

} // namespace emotes
