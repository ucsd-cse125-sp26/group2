#pragma once
#include "../game/World.hpp"
#include "GpuTypes.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

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

    // Create the renderer — uploads world geometry, builds pipeline.
    // Returns false on failure.
    bool init(SDL_GPUDevice* gpu, SDL_Window* window, const World& world);

    // Resize depth texture when window changes size
    void onResize(uint32_t w, uint32_t h);

    // Draw the world with the given uniforms
    void drawWorld(SDL_GPUCommandBuffer* cmdbuf, SDL_GPUTexture* swapchain, const SceneUniforms& uniforms);

    void destroy();

private:
    SDL_GPUShader* loadShader(const char* filename, SDL_GPUShaderStage stage, uint32_t uniformBuffers) const;
    void createDepthTexture(uint32_t w, uint32_t h);
    SDL_GPUBuffer* uploadVertexBuffer(const std::vector<Vertex>& verts) const;
};
