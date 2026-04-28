#pragma once

#include "Asset.hpp"
#include "Camera.hpp"
#include "renderer/Camera.hpp" // legacy Camera type required by IRenderer contract
#include "renderer/IRenderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>
#include <string>
#include <vector>

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texUV;
};

/// @brief Graphics-team's work-in-progress SDL3 GPU renderer.
///
/// Implements `IRenderer` but only a handful of methods are meaningful today
/// (`init`, `drawFrame`, `quit`). Everything else returns a no-op / sentinel;
/// `supports(...)` reflects the truth so `HybridRenderer` routes unimplemented
/// calls back to the legacy renderer.
///
/// As each feature comes online here, flip its entry on in `supports(...)` and
/// remove the corresponding pass-through from the legacy renderer when ready.
///
/// Shaders: `shaders/projective.vert` + `shaders/normal.frag`
/// (compiled GLSL → SPIR-V at build time via glslc/glslangValidator).
class NewRenderer : public IRenderer
{
public:
    [[nodiscard]] bool supports(RendererFeature feature) const override;

    bool init(SDL_Window* window) override;
    bool init(SDL_Window* window, SDL_GPUDevice* sharedDevice);
    void drawFrame(glm::vec3 eye, float yaw, float pitch, float roll) override;
    void quit() override;

    // ---- Unimplemented IRenderer methods ----
    // Marked as "not supported" via `supports()`. HybridRenderer never calls
    // these, but they must exist for the class to be concrete.
    [[nodiscard]] SDL_GPUDevice* getDevice() const override { return device_; }
    [[nodiscard]] SDL_GPUShaderFormat getShaderFormat() const override { return shaderFormat_; }
    [[nodiscard]] const Camera& getCamera() const override { return legacyCameraStub_; }

    void setParticleSystem(ParticleSystem* /*ps*/) override {}
    int loadSceneModel(const char* /*filename*/, glm::vec3 /*pos*/, float /*scale*/, bool /*flipUVs*/) override
    {
        return -1;
    }
    int uploadSceneModel(const LoadedModel& /*model*/) override { return -1; }
    bool setVSync(bool /*enabled*/) override { return false; }
    void updateModelMeshVertices(int /*modelIndex*/,
                                 int /*meshIndex*/,
                                 const ModelVertex* /*vertices*/,
                                 Uint32 /*vertexCount*/) override
    {}
    void setEntityRenderList(std::vector<EntityRenderCmd> /*cmds*/) override {}
    void setPointLights(std::vector<PointLight> /*lights*/) override {}
    void setModelEmissive(int /*modelIndex*/, glm::vec4 /*emissiveFactor*/) override {}
    void setModelScenePass(int /*modelIndex*/, bool /*drawInScene*/) override {}
    void setWeaponViewmodel(const WeaponViewmodel& /*vm*/) override {}
    void requestScreenshot(const std::string& /*path*/) override {}
    [[nodiscard]] int modelCount() const override { return 0; }

private:
    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShaderFormat shaderFormat_ = SDL_GPU_SHADERFORMAT_INVALID;

    bool ownsDevice_ = false;
    bool ownsWindowClaim_ = false;

    SDL_GPUGraphicsPipeline* geometryPipeline_ = nullptr;
    SDL_GPUTexture* depthTexture_ = nullptr;

    // Temp for single texture
    SDL_GPUTexture* texture_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;

    Uint32 depthWidth_ = 0;
    Uint32 depthHeight_ = 0;

    NewCamera camera_;
    Camera legacyCameraStub_{};

    bool initCommon();
    bool createGeometryPipeline();
    bool loadSceneAssets();
    bool ensureDepthTexture(Uint32 width, Uint32 height);

    void createMeshBuffers(MeshIdInt meshId);
    void drawMesh(SDL_GPURenderPass* renderPass, const Asset::Mesh& mesh) const;
};
