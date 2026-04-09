#pragma once

#include "ecs/registry/Registry.hpp"
#include "particles/ParticleTypes.hpp"

#include <glm/glm.hpp>
#include <vector>

/// @brief Builds camera-facing ribbon trails for slow/arcing projectiles (rockets).
///
/// Iterates entities with RibbonEmitter each frame, ages their node ring-buffers,
/// then expands all segments into a flat RibbonVertex array for GPU upload.
class RibbonTrail
{
public:
    /// @brief Rebuild the vertex staging buffer from all active RibbonEmitter entities.
    void update(float dt, Registry& registry, glm::vec3 camPos);

    [[nodiscard]] const RibbonVertex* data() const { return vertices_.data(); }
    [[nodiscard]] uint32_t count() const { return static_cast<uint32_t>(vertices_.size()); }

private:
    std::vector<RibbonVertex> vertices_; // rebuilt every frame
};
