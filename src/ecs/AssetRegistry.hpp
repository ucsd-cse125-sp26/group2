/// @file AssetRegistry.hpp
/// @brief Central registry mapping named assets to renderer model indices.
///
/// Every loaded model (map, characters, weapons, props) goes through this
/// registry so there is a single source of truth for "model index N = what."
/// Both client and server can query asset IDs by name; only the client
/// populates the renderer model index.
///
/// Props can be loaded with collision, producing collision data that is merged
/// into the active world.

#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
#include <string>
#include <unordered_map>
#include <vector>

/// @brief Describes how a registered asset should be used.
enum class AssetRole : uint8_t
{
    Map,    ///< Static world geometry — rendered in scene pass, has collision.
    Prop,   ///< Static placed object — rendered in scene pass, optionally has collision.
    Entity, ///< Dynamic entity model (player, weapon) — drawn via EntityRenderCmd only.
    Effect, ///< Procedural effect (glow sphere, beam) — drawn via EntityRenderCmd only.
};

/// @brief One entry in the asset registry.
struct AssetEntry
{
    std::string name;                      ///< Human-readable name (e.g. "porsche", "wraith", "map1").
    std::string filename;                  ///< GLB filename relative to assets/ (empty for procedural).
    AssetRole role = AssetRole::Entity;
    glm::vec3 renderScale{1.0f};           ///< Default per-entity render scale for this asset.
    glm::vec3 renderTranslation{0.0f};     ///< Default per-entity local translation for this asset.
    glm::vec3 renderRotationDegrees{0.0f}; ///< Default per-entity local Euler rotation in degrees.
    int32_t modelIndex = -1;               ///< Renderer model index (-1 = not uploaded to GPU yet).
    bool hasCollision = false;             ///< True if collision was generated for this asset.
};

/// @brief Central asset registry — maps names to renderer model indices.
///
/// Usage:
///   int id = assets.add("porsche", "free_1975_porsche_911_930_turbo.glb", AssetRole::Prop);
///   assets.setModelIndex(id, renderer.loadSceneModel(...));
///   int modelIdx = assets.modelIndex("porsche");  // → the renderer index
///   int modelIdx = assets.modelIndex(id);          // → same thing
class AssetRegistry
{
public:
    /// @brief Register a new asset. Returns its asset ID (index into entries_).
    int add(const std::string& name,
            const std::string& filename = "",
            AssetRole role = AssetRole::Entity,
            glm::vec3 renderScale = glm::vec3{1.0f},
            glm::vec3 renderTranslation = glm::vec3{0.0f},
            glm::vec3 renderRotationDegrees = glm::vec3{0.0f})
    {
        const int id = static_cast<int>(entries_.size());
        entries_.push_back({.name = name,
                            .filename = filename,
                            .role = role,
                            .renderScale = renderScale,
                            .renderTranslation = renderTranslation,
                            .renderRotationDegrees = renderRotationDegrees});
        nameToId_[name] = id;
        return id;
    }

    /// @brief Set the renderer model index for a registered asset.
    void setModelIndex(int assetId, int32_t modelIndex)
    {
        if (assetId >= 0 && assetId < static_cast<int>(entries_.size()))
            entries_[static_cast<size_t>(assetId)].modelIndex = modelIndex;
    }

    /// @brief Mark an asset as having collision data.
    void setHasCollision(int assetId, bool v = true)
    {
        if (assetId >= 0 && assetId < static_cast<int>(entries_.size()))
            entries_[static_cast<size_t>(assetId)].hasCollision = v;
    }

    /// @brief Look up the renderer model index by asset name. Returns -1 if not found.
    [[nodiscard]] int32_t modelIndex(const std::string& name) const
    {
        const auto it = nameToId_.find(name);
        if (it == nameToId_.end())
            return -1;
        return entries_[static_cast<size_t>(it->second)].modelIndex;
    }

    /// @brief Look up the renderer model index by asset ID. Returns -1 if out of range.
    [[nodiscard]] int32_t modelIndex(int assetId) const
    {
        if (assetId < 0 || assetId >= static_cast<int>(entries_.size()))
            return -1;
        return entries_[static_cast<size_t>(assetId)].modelIndex;
    }

    /// @brief Look up asset ID by name. Returns -1 if not found.
    [[nodiscard]] int id(const std::string& name) const
    {
        const auto it = nameToId_.find(name);
        return (it != nameToId_.end()) ? it->second : -1;
    }

    /// @brief Get a read-only reference to an entry.
    [[nodiscard]] const AssetEntry& entry(int assetId) const { return entries_[static_cast<size_t>(assetId)]; }

    /// @brief Number of registered assets.
    [[nodiscard]] int count() const { return static_cast<int>(entries_.size()); }

    /// @brief All entries (for debug UI / logging).
    [[nodiscard]] const std::vector<AssetEntry>& entries() const { return entries_; }

private:
    std::vector<AssetEntry> entries_;
    std::unordered_map<std::string, int> nameToId_;
};
