/// @file RibbonEmitter.hpp
/// @brief Ribbon trail emitter component for slow projectiles.

#pragma once

#include <glm/glm.hpp>

/// @brief Component attached to slow/arcing projectile entities (rockets, slow bolts).
///
/// Records a ring-buffer of historical positions; the particle system expands
/// consecutive node pairs into camera-facing ribbon quads each frame.
struct RibbonEmitter
{
    static constexpr int MaxNodes = 32;

    /// @brief A single recorded point along the ribbon trail.
    struct Node
    {
        glm::vec3 pos{}; ///< World-space position of this node.
        float age = 0.f; ///< Seconds since this node was recorded.
    };

    Node nodes[MaxNodes]{};
    int count = 0;                             ///< Number of live nodes.
    int head = 0;                              ///< Ring-buffer insertion index.

    float width = 4.f;                         ///< Half-width of ribbon in world units.
    float maxAge = 0.4f;                       ///< Nodes older than this are dropped.
    float recordInterval = 0.016f;             ///< Seconds between node recordings (~60 Hz).
    float recordAccumulator = 0.f;             ///< Accumulator for sub-frame node recording.

    glm::vec4 tipColor{1.f, 0.6f, 0.1f, 1.f};  ///< Color at the rocket tip (newest node).
    glm::vec4 tailColor{1.f, 0.3f, 0.0f, 0.f}; ///< Color at the tail (fades to transparent).
};
