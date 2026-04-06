#pragma once
#include "IRenderer.hpp"

#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// SDLGPURenderer — draws the RGB triangle via the SDL3 GPU pipeline.
// The caller owns the command buffer and render pass; this class only
// records triangle draw calls into the supplied render pass.
// ---------------------------------------------------------------------------
class SDLGPURenderer : public IRenderer
{
public:
    bool init(SDL_Window* window) override;
    void draw(SDL_GPURenderPass* renderPass) override;
    [[nodiscard]] SDL_GPUDevice* device() const override { return gpuDevice; }
    void shutdown() override;

private:
    SDL_Window* gpuWindow = nullptr;
    SDL_GPUDevice* gpuDevice = nullptr;
    SDL_GPUGraphicsPipeline* gpuPipeline = nullptr;
};
