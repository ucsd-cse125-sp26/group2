/// @file PlayerName.hpp
/// @brief Per-player display name (nickname) — replicated to clients.

#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <type_traits>

/// @brief Replicated player display name.
///
/// Server picks an unused entry from `player_nicknames::k_nicknames` on
/// connect and stores it here.  Clients read it for the kill feed,
/// scoreboard, world-space enemy bars, death cards, etc., so players
/// always see human-friendly handles ("WOLF kills FOX") instead of raw
/// `Player #3` style fallbacks.
///
/// `isCustom` is reserved for the future "user sets their own nickname"
/// flow: when true the server preserves the buffer instead of re-rolling
/// on respawn / reconnect, and the auto-assigner skips this player.
///
/// Stored as a fixed-size buffer so the component stays trivially
/// copyable — the existing `OutputArchive` / `InputArchive` only handle
/// trivially-copyable types, so a `std::string` field would not
/// replicate.  Names are clamped to `k_maxLen` chars + NUL.
struct PlayerName
{
    static constexpr std::size_t k_maxLen = 23;

    std::array<char, k_maxLen + 1> name{}; ///< NUL-terminated UTF-8.  All zeros = unset.
    bool isCustom = false;                 ///< True once the player picks their own name.

    /// @brief Copy a NUL-terminated source into the bounded buffer.
    /// Truncates silently and always writes a terminating NUL.
    void set(const char* src)
    {
        if (src == nullptr) {
            name[0] = '\0';
            return;
        }
        std::size_t i = 0;
        for (; i < k_maxLen && src[i] != '\0'; ++i)
            name[i] = src[i];
        name[i] = '\0';
    }

    /// @brief Read the name as a C string.  Always NUL-terminated.
    [[nodiscard]] const char* c_str() const { return name.data(); }

    /// @brief Whether this nickname slot has any text in it.
    [[nodiscard]] bool empty() const { return name[0] == '\0'; }
};

static_assert(std::is_trivially_copyable_v<PlayerName>,
              "PlayerName must stay trivially copyable for the existing snapshot serializer.");
