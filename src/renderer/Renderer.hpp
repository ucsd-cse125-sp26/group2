#pragma once
#include "../game/World.hpp"
#include "GpuTypes.hpp"
#include "MeshGen.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// Per-player GPU resources (one set per player slot)
// ---------------------------------------------------------------------------

struct PlayerModel
{
    SDL_GPUBuffer* vbuf = nullptr;
    uint32_t vcount     = 0;
    glm::vec3 color     = {1.0f, 1.0f, 1.0f};
};

// ---------------------------------------------------------------------------
// Fragment tint pushed per draw call via SDL_PushGPUFragmentUniformData
// ---------------------------------------------------------------------------

struct FragTint
{
    glm::vec4 tint; // xyz = RGB, w = alpha
};

struct Renderer
{
    SDL_GPUDevice* gpu                = nullptr;
    SDL_Window* window                = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUBuffer* worldVBuf          = nullptr;
    SDL_GPUTexture* depthTex          = nullptr;
    uint32_t worldVCount              = 0;
    uint32_t windowW                  = 0;
    uint32_t windowH                  = 0;

    // One pre-built player model per slot (uploaded at init)
    std::array<PlayerModel, 4> playerModels;

    bool init(SDL_GPUDevice* gpu, SDL_Window* window, const World& world);
    void onResize(uint32_t w, uint32_t h);

    // Draw the world + all visible players in one render pass.
    // playerPositions[i] = world-space position (feet), playerAlive[i] = visibility mask.
    // localPlayerId: whose player model to omit (we don't render ourselves in first-person).
    void drawScene(SDL_GPUCommandBuffer* cmdbuf,
                   SDL_GPUTexture* swapchain,
                   const SceneUniforms& viewProjUniforms,
                   const glm::vec3 playerPositions[4],
                   const float playerYaws[4],
                   const bool playerAlive[4],
                   int localPlayerId);

    void destroy();

private:
    SDL_GPUShader* loadShader(const char* filename,
                              SDL_GPUShaderStage stage,
                              uint32_t vertUniformBufs,
                              uint32_t fragUniformBufs = 0) const;
    void createDepthTexture(uint32_t w, uint32_t h);
    SDL_GPUBuffer* uploadVertexBuffer(const std::vector<Vertex>& verts) const;
};
