/// @file SurfaceType.hpp
/// @brief Surface-material classification shared across collision, raycasts,
///        VFX, SFX, and damage falloff.
///
/// Lives in the physics layer because primitives carry a per-instance
/// `SurfaceType` (cooked from Blender material names) that drives the
/// downstream gameplay systems.  Projectile.hpp re-includes this header
/// to preserve the existing `#include "Projectile.hpp"` paths that read
/// the enum.

#pragma once

#include <cstdint>
#include <string_view>

/// @brief Surface material hit by a projectile / hitscan / contact.
///
/// **Cook-time mapping** (Blender material name → enum, case-insensitive):
///   - `mat_metal`    → Metal
///   - `mat_concrete` → Concrete  (also the default for unprefixed names)
///   - `mat_flesh`    → Flesh
///   - `mat_wood`     → Wood
///   - `mat_energy`   → Energy
///
/// Unknown / unprefixed materials fall back to `Concrete`, matching the
/// hard-coded behaviour of the original (pre-Phase-3) raycast code.
enum class SurfaceType : uint8_t
{
    Metal = 0,
    Concrete = 1,
    Flesh = 2,
    Wood = 3,
    Energy = 4,
    Count = 5,
};

/// @brief Parse a Blender material name into a `SurfaceType`.
///
/// Accepts either the bare suffix ("metal") or the `mat_` prefix ("mat_metal").
/// Case-insensitive.  Returns `Concrete` for unrecognised input — matches the
/// pre-Phase-3 default behaviour so missing material tags never break a map.
[[nodiscard]] constexpr SurfaceType surfaceTypeFromMaterialName(std::string_view name) noexcept
{
    constexpr auto k_eqIgnoreCase = [](std::string_view a, std::string_view b) constexpr noexcept {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i) {
            const char ca = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] + 32) : a[i];
            const char cb = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] + 32) : b[i];
            if (ca != cb)
                return false;
        }
        return true;
    };

    // Strip optional `mat_` prefix.
    if (name.size() >= 4) {
        const std::string_view prefix = name.substr(0, 4);
        if (k_eqIgnoreCase(prefix, "mat_"))
            name = name.substr(4);
    }

    if (k_eqIgnoreCase(name, "metal"))
        return SurfaceType::Metal;
    if (k_eqIgnoreCase(name, "concrete"))
        return SurfaceType::Concrete;
    if (k_eqIgnoreCase(name, "flesh"))
        return SurfaceType::Flesh;
    if (k_eqIgnoreCase(name, "wood"))
        return SurfaceType::Wood;
    if (k_eqIgnoreCase(name, "energy"))
        return SurfaceType::Energy;
    return SurfaceType::Concrete;
}

/// @brief Display name for a `SurfaceType`.  Used in debug UI / logging.
[[nodiscard]] constexpr std::string_view surfaceTypeName(SurfaceType s) noexcept
{
    switch (s) {
    case SurfaceType::Metal:
        return "Metal";
    case SurfaceType::Concrete:
        return "Concrete";
    case SurfaceType::Flesh:
        return "Flesh";
    case SurfaceType::Wood:
        return "Wood";
    case SurfaceType::Energy:
        return "Energy";
    case SurfaceType::Count:
        break;
    }
    return "?";
}
