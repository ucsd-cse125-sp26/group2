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
    void draw(SDL_GPURenderPass* render_pass) override;
    SDL_GPUDevice* device() const override { return device_; }
    void shutdown() override;

private:
    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
};
