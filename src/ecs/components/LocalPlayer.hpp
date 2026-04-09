#pragma once

/// @brief Marker component that tags exactly one entity per client as the locally controlled player.
///
/// Used by InputSampleSystem to distinguish the local player from remote player entities.
/// Remote entities also carry InputSnapshot for server-side simulation, but must never
/// be overwritten by local input.
struct LocalPlayer
{};
