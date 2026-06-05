/// @file BeamState.hpp
/// @brief Continuous beam weapon state component.
///
/// Updated by WeaponSystem each server tick when a beam weapon is firing.
/// Synced to clients via registry serialization so the renderer can draw
/// the beam visual every frame.

#pragma once

#include "Projectile.hpp"

#include <glm/vec3.hpp>

/// @brief Per-entity state of a continuous beam weapon.
///
/// When `active` is true the renderer draws a glow cylinder from `origin`
/// to `hitPoint` and places dynamic point lights along the beam.
struct BeamState
{
    bool active{false};                     ///< True while the beam is firing.
    uint8_t _pad[3]{};                      ///< Padding for alignment.
    WeaponType type{WeaponType::EnergyGun}; ///< Weapon type (selects colour / VFX).
    uint8_t locked{0};                      ///< True when the energy beam is damaging a locked target.
    uint8_t _pad2[2]{};                     ///< Padding for alignment.
    float lockStrength{0.0f};               ///< 0..1 visual ramp for Tesla arc attachment.
    glm::vec3 origin{0.0f};                 ///< World-space beam start (eye position).
    glm::vec3 hitPoint{0.0f};               ///< World-space beam end (hit or max range).
    glm::vec3 guidePoint{0.0f};             ///< Straight-ahead endpoint used before arcing toward a target.
};

static_assert(std::is_trivially_copyable_v<BeamState>,
              "BeamState must be trivially copyable for network serialization");
