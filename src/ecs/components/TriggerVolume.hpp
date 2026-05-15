/// @file TriggerVolume.hpp
/// @brief Trigger-volume marker component.
///
/// Attach this alongside `Position` and `CollisionShape` to turn an entity
/// into a sensor (no physical response — overlap events only).
/// `TriggerSystem` reads the entity's collision shape to compute overlaps
/// each tick and emits Enter / Stay / Exit events.

#pragma once

#include <cstdint>

/// @brief Marker + filter data for a trigger-volume entity.
struct TriggerVolume
{
    /// @brief Bitmask of entity "layers" this trigger accepts.  An entity must
    /// have at least one bit in common with `layerMask` to generate events.
    /// Default 0xFFFFFFFF = accept all layers.
    uint32_t layerMask = 0xFFFFFFFFu;

    /// @brief If false (default), client-side prediction does NOT emit local
    /// events — only the server's authoritative emission applies (state
    /// changes are then replicated normally).  Set to true for triggers whose
    /// events drive purely-local UI feedback (e.g. "entering capture zone"
    /// banner) and don't affect simulation state.
    bool fireOnPredictedClient = false;
};

/// @brief Optional layer assignment for an entity.  When absent, the entity
/// is treated as belonging to all layers (matches any trigger).
struct CollisionLayer
{
    uint32_t bits = 0xFFFFFFFFu;
};
