/// @file PlayerNicknames.hpp
/// @brief Curated animal-handle list used as default player nicknames.
///
/// On connect, ServerGame picks the least-used slot from this array (ties
/// broken by lowest index for deterministic assignment across reconnects)
/// and writes the matching string into the new player's `PlayerName`
/// component.  Mirrors `player_colors::k_palette`'s pattern exactly —
/// keeping the two systems parallel makes "what handle do I get?" follow
/// the same mental model as "what tint do I get?".
///
/// Adding a new nickname is drop-in: extend the array.  Removing one mid-
/// match is also fine; in-flight players keep their assigned name (it's
/// stored in their replicated component, not by index).
///
/// All-caps to match the Voidfall mil-spec aesthetic.

#pragma once

#include <array>

namespace player_nicknames
{

// `const char*` rather than `std::string_view` — the assigner reaches
// into `PlayerName::set(const char*)` which expects a NUL-terminated
// source; `string_view::data()` doesn't promise that.
inline constexpr std::array<const char*, 24> k_nicknames = {
    // Earth predators / raptors — instantly recognisable.
    "WOLF",
    "FOX",
    "HAWK",
    "OWL",
    "RAVEN",
    "LYNX",
    "OTTER",
    "SHARK",
    "ORCA",
    "EAGLE",
    "TIGER",
    "COBRA",
    "VIPER",
    "MANTIS",
    "JAGUAR",
    "PANTHER",
    "KESTREL",
    "BADGER",
    "FALCON",
    "MOOSE",
    "BISON",
    // Mythological apex picks — round out the pool past the realistic ones.
    "KRAKEN",
    "PHOENIX",
    "GRYPHON",
};

inline constexpr int k_nicknameCount = static_cast<int>(k_nicknames.size());

} // namespace player_nicknames
