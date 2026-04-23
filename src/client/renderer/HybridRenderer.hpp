/// @file HybridRenderer.hpp
/// @brief Routing layer between the legacy renderer and the new (in-progress)
/// renderer. Each `IRenderer` call dispatches to the new renderer if it
/// reports `supports(feature) == true`, otherwise falls through to legacy.
///
/// This lets the graphics team replace functionality piece-by-piece: every
/// time a method is implemented in `NewRenderer`, its entry in
/// `NewRenderer::supports()` is flipped on and the hybrid starts routing that
/// call to the new implementation without any changes at the call sites.

#pragma once

#include "IRenderer.hpp"
#include "Renderer.hpp"
#include "renderer-new/Renderer.hpp"

class HybridRenderer : public IRenderer
{
public:
    HybridRenderer();

    [[nodiscard]] bool supports(RendererFeature feature) const override;

    bool init(SDL_Window* window) override;
    void drawFrame(glm::vec3 eye, float yaw, float pitch, float roll) override;
    void quit() override;

    [[nodiscard]] SDL_GPUDevice* getDevice() const override;
    [[nodiscard]] SDL_GPUShaderFormat getShaderFormat() const override;
    [[nodiscard]] const Camera& getCamera() const override;

    void setParticleSystem(ParticleSystem* ps) override;
    int loadSceneModel(const char* filename, glm::vec3 pos, float scale, bool flipUVs = false) override;
    int uploadSceneModel(const LoadedModel& model) override;
    bool setVSync(bool enabled) override;
    void
    updateModelMeshVertices(int modelIndex, int meshIndex, const ModelVertex* vertices, Uint32 vertexCount) override;
    void setEntityRenderList(std::vector<EntityRenderCmd> cmds) override;
    void setPointLights(std::vector<PointLight> lights) override;
    void setWeaponViewmodel(const WeaponViewmodel& vm) override;
    void requestScreenshot(const std::string& path) override;
    [[nodiscard]] int modelCount() const override;

    /// @brief HDR render target format (RGBA16F). Pass-through to the legacy
    /// renderer's static constant so existing call sites keep working.
    [[nodiscard]] static constexpr SDL_GPUTextureFormat getHdrFormat() { return Renderer::getHdrFormat(); }

    /// @brief Direct access to the legacy renderer instance, needed by
    /// `DebugUI::buildLightingUI` / `buildSkyboxUI` which reach into the
    /// legacy renderer's public lighting/skybox fields.
    [[nodiscard]] Renderer& legacy() { return legacy_; }
    [[nodiscard]] const Renderer& legacy() const { return legacy_; }

    /// @brief Direct access to the new renderer instance (not normally needed).
    [[nodiscard]] NewRenderer& next() { return next_; }
    [[nodiscard]] const NewRenderer& next() const { return next_; }

    /// @brief Convenience aliases for fields Game reaches into directly. These
    /// reference the legacy renderer's state so existing call sites keep
    /// working verbatim. When the new renderer grows equivalent state, these
    /// can be routed differently.
    int& ssrMode;           ///< Alias of legacy_.ssrMode.
    RenderToggles& toggles; ///< Alias of legacy_.toggles.

private:
    Renderer legacy_;
    NewRenderer next_;

    /// @brief Track which backend is actually initialised so `quit()` can
    /// tear down exactly what `init()` brought up, in the right order.
    bool legacyInitialised_ = false;
    bool nextInitialised_ = false;
    bool drawFrameLogged_ = false; ///< True after the first drawFrame routing decision is logged.
};
