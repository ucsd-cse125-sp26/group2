#pragma once
#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// IRenderer — interface for the active rendering backend.
//
// init()     — create GPU device / context, pipeline.
// draw()     — record draw commands into an already-open render pass.
//              For SDL3 GPU path: renderPass is a live SDL_GPURenderPass*.
//              For OpenGL path:   renderPass is ignored (nullptr).
// device()   — returns the SDL_GPUDevice* (SDL3 GPU path) or nullptr (GL).
// shutdown() — destroy all GPU objects.
// ---------------------------------------------------------------------------
class IRenderer
{
public:
    virtual ~IRenderer() = default;
    virtual bool init(SDL_Window* window) = 0;
    virtual void draw(SDL_GPURenderPass* renderPass) = 0;
    [[nodiscard]] virtual SDL_GPUDevice* device() const = 0;
    virtual void shutdown() = 0;
};
