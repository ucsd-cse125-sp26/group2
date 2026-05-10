/// @file NewRenderer.hpp
/// @brief Work-in-progress SDL3 GPU renderer.

#pragma once

#include "Asset.hpp"
#include "Camera.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>
#include <string>

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texUV;
};

/// @brief Graphics-team's work-in-progress SDL3 GPU renderer.
///
/// Shaders: `shaders-new/geometry.vert` + `shaders-new/geometry.frag`.
class NewRenderer
{
public:
    bool init(SDL_Window* window);
    void drawFrame(glm::vec3 eye, float yaw, float pitch, float roll);
    void quit();

    [[nodiscard]] SDL_GPUDevice* getDevice() const { return device_; }
    [[nodiscard]] SDL_GPUShaderFormat getShaderFormat() const { return shaderFormat_; }
    [[nodiscard]] const NewCamera& getCamera() const { return camera_; }
    void setHudTexture(SDL_GPUTexture* hudTexture);

    int loadSceneModel(
        const char* filename, glm::vec3 pos, float scale, bool flipUVs, const std::string& excludeNodesContaining = "");

private:
    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShaderFormat shaderFormat_ = SDL_GPU_SHADERFORMAT_INVALID;

    SDL_GPUGraphicsPipeline* geometryPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* hudPipeline_ = nullptr;

    //SDL_GPUTexture* depthTexture_ = nullptr;
    SDL_GPUDepthStencilTargetInfo depthTarget_{};

    // Temp for single texture
    SDL_GPUTexture* texture_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;

    SDL_GPUTexture* hudTexture_ = nullptr;
    SDL_GPUSampler* hudSampler_ = nullptr;

    Uint32 depthWidth_ = 0;
    Uint32 depthHeight_ = 0;

    NewCamera camera_;

    /// @brief Build the geometry graphics pipeline from vertex/fragment shaders.
    /// @return True on success.
    bool createGeometryPipeline();

    bool createHudPipeline();

    /// @brief Load models via AssetLoader and upload their mesh buffers to the GPU.
    /// @return True on success.
    bool loadSceneAssets();

    /// @brief (Re-)create the depth texture if the viewport size changed.
    /// @param width  New viewport width in pixels.
    /// @param height New viewport height in pixels.
    /// @return True on success.
    bool ensureDepthTextureSize(Uint32 width, Uint32 height);

    /// @brief Allocate GPU vertex and index buffers for the given mesh.
    /// @param meshId Key into Asset::meshes_.
    void createMeshBuffers(MeshIdInt meshId) const;

    void drawGeometryPass(SDL_GPUTexture *swapchain,SDL_GPUCommandBuffer *cmd);

    void drawUIPass(SDL_GPUTexture *swapchain,SDL_GPUCommandBuffer *cmd);

    void drawWorldModelInstances(SDL_GPURenderPass* renderPass,SDL_GPUCommandBuffer *cmd);

    void drawWeapon(SDL_GPURenderPass *geometryPass,SDL_GPUCommandBuffer *cmd);

    void drawModel(ModelIdInt modelId, const glm::mat4& modelTransform,SDL_GPURenderPass* renderPass,SDL_GPUCommandBuffer *cmd);

    /// @brief Bind a mesh's buffers and issue an indexed draw call.
    /// @param renderPass The active render pass.
    /// @param mesh The mesh to draw.
    void drawMesh(SDL_GPURenderPass* renderPass, const Asset::Mesh& mesh) const;

    void drawHud(SDL_GPURenderPass* pass);

    void setMainCamera(glm::vec3 eye, float yaw, float pitch, float roll,Uint32 width,Uint32 height);
};
