#pragma once

#include "IRenderer.hpp"

#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// SDLGPURenderer — RGB triangle via the SDL3 GPU pipeline.
//
// Shaders: shaders/triangle.vert + shaders/triangle.frag
//   Compiled to SPIR-V at build time by cmake (glslangValidator / glslc).
//   On Release builds the SPIR-V is embedded into the binary
//   (BUNDLE_SHADERS=ON by default for Release/RelWithDebInfo).
// ---------------------------------------------------------------------------

class SDLGPURenderer : public IRenderer
{
public:
    bool init(SDL_Window* window) override;
    void renderFrame() override;
    void shutdown() override;

private:
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
};
