#pragma once

#include "Asset.hpp"
#include "Camera.hpp"
#include "renderer/Camera.hpp" // legacy Camera type required by IRenderer contract
#include "renderer/IRenderer.hpp"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>
#include <vector>

struct Matrices
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

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
    std::string shadersDir_;
    /// @brief Report which `RendererFeature` entries the new renderer implements.
    [[nodiscard]] bool supports(RendererFeature feature) const override;

    /// @brief Initialise the GPU device, pipeline, and ImGui GPU backend.
    /// @param window  The SDL window to render into.
    /// @return False on any fatal GPU error.
    /// @pre An ImGui context must already exist (created by DebugUI::init).
    bool init(SDL_Window* window) override;

    /// @brief Initialise against a pre-existing GPU device (shared with another
    /// renderer). The provided device must already have claimed `window`.
    /// Used by `HybridRenderer` so both renderers operate on the same device
    /// — SDL3 only allows a single device to claim a given window.
    bool init(SDL_Window* window, SDL_GPUDevice* sharedDevice);

    /// @brief Submit the scene geometry and ImGui draw data for one frame.
    /// @param eye    World-space camera eye position (interpolated, in Quake units).
    /// @param yaw    Horizontal look angle in radians (matches InputSnapshot::yaw).
    /// @param pitch  Vertical look angle in radians (positive = looking down).
    /// @param roll   Camera roll in radians (currently ignored by the new renderer).
    void drawFrame(glm::vec3 eye, float yaw, float pitch, float roll) override;

    /// @brief Release all GPU resources. Waits for GPU idle before freeing.
    /// @pre Call before the SDL window is destroyed.
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
    void setWeaponViewmodel(const WeaponViewmodel& /*vm*/) override {}
    void requestScreenshot(const std::string& /*path*/) override {}
    [[nodiscard]] int modelCount() const override { return 0; }

private:
    SDL_Window* window_ = nullptr;                ///< The SDL window being rendered into.
    SDL_GPUDevice* device_ = nullptr;             ///< The SDL GPU device.
    SDL_GPUShaderFormat shaderFormat_ = SDL_GPU_SHADERFORMAT_INVALID;
    bool ownsDevice_ = false;                     ///< True if we created device_ ourselves.
    bool ownsWindowClaim_ = false;                ///< True if we called ClaimWindow.
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr; ///< The scene graphics pipeline.

    float fovyDegrees_ = 60.0f;
    float nearPlane_ = 5.0f;
    float farPlane_ = 15000.0f;

    NewCamera camera_;

    SDL_GPUTexture* depthTexture = nullptr;
    Uint32 depthWidth_ = 0;
    Uint32 depthHeight_ = 0;

    Asset::GeoBufferInfo vBufferInfo_;
    Asset::GeoBufferInfo iBufferInfo_;

    Camera legacyCameraStub_{}; ///< Default-constructed legacy camera used to satisfy getCamera().

    bool ensureDepthTexture(Uint32 w, Uint32 h);

    [[nodiscard]] SDL_GPUTransferBuffer* createTransferBuffer(size_t transferBufferSize, bool upload) const;
    [[nodiscard]] SDL_GPUBuffer* createGPUBuffer(size_t bufferSize, SDL_GPUBufferUsageFlags usage) const;
    void uploadDataToGPUBuffer(SDL_GPUCommandBuffer* cmd,
                               const std::vector<Asset::GeoBufferInfo>& modelBuffersInfo) const;
    void drawMesh(SDL_GPURenderPass* renderPass, SDL_GPUIndexElementSize iElementSizeSdlType, Asset::Mesh m);

    bool initCommon(SDL_Window* window);
    void genMeshBuffers(MeshIdInt meshId);
};
