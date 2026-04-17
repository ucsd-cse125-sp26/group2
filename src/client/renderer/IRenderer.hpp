/// @file IRenderer.hpp
/// @brief Abstract renderer interface used by the hybrid dispatcher to route
/// calls to either the legacy renderer or a (partially-implemented) new one.
///
/// The graphics team is rebuilding the renderer in `renderer-new/`. As each
/// method is re-implemented there, the corresponding entry is flipped on in
/// `NewRenderer::supports(...)` and the `HybridRenderer` starts routing that
/// call to the new implementation instead of the legacy one.

#pragma once

#include "Camera.hpp"
#include "ModelLoader.hpp"
#include "RendererTypes.hpp" // EntityRenderCmd, WeaponViewmodel, RenderToggles

#include <SDL3/SDL.h>

#include <glm/glm.hpp>
#include <string>
#include <vector>

class ParticleSystem;

/// @brief Stable identifiers for each dispatchable renderer method.
///
/// When adding a new routable method to `IRenderer`, add an enum entry here
/// and teach `HybridRenderer` to check it. The new renderer only needs to
/// report `true` from `supports()` for the features it has implemented; the
/// rest fall through to the legacy renderer.
enum class RendererFeature
{
    Init,
    DrawFrame,
    Quit,
    GetDevice,
    GetShaderFormat,
    GetCamera,
    SetParticleSystem,
    LoadSceneModel,
    UploadSceneModel,
    SetVSync,
    UpdateModelMeshVertices,
    SetEntityRenderList,
    SetWeaponViewmodel,
    RequestScreenshot,
    ModelCount,
};

/// @brief Abstract renderer contract. Both the legacy and the new renderer
/// implement this; `HybridRenderer` routes per-method based on `supports()`.
class IRenderer
{
public:
    virtual ~IRenderer() = default;

    /// @brief Return true iff this renderer implements the given feature.
    [[nodiscard]] virtual bool supports(RendererFeature feature) const = 0;

    virtual bool init(SDL_Window* window) = 0;
    virtual void drawFrame(glm::vec3 eye, float yaw, float pitch, float roll) = 0;
    virtual void quit() = 0;

    [[nodiscard]] virtual SDL_GPUDevice* getDevice() const = 0;
    [[nodiscard]] virtual SDL_GPUShaderFormat getShaderFormat() const = 0;
    [[nodiscard]] virtual const Camera& getCamera() const = 0;

    virtual void setParticleSystem(ParticleSystem* ps) = 0;
    virtual int loadSceneModel(const char* filename, glm::vec3 pos, float scale, bool flipUVs) = 0;
    virtual int uploadSceneModel(const LoadedModel& model) = 0;
    virtual bool setVSync(bool enabled) = 0;
    virtual void
    updateModelMeshVertices(int modelIndex, int meshIndex, const ModelVertex* vertices, Uint32 vertexCount) = 0;
    virtual void setEntityRenderList(std::vector<EntityRenderCmd> cmds) = 0;
    virtual void setWeaponViewmodel(const WeaponViewmodel& vm) = 0;
    virtual void requestScreenshot(const std::string& path) = 0;
    [[nodiscard]] virtual int modelCount() const = 0;
};
