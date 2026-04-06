#pragma once
#include "IRenderer.hpp"

#include <SDL3/SDL.h>

class OpenGLRenderer : public IRenderer
{
public:
    bool init(SDL_Window* window) override;
    void draw(SDL_GPURenderPass* /*unused*/) override;
    SDL_GPUDevice* device() const override { return nullptr; }
    void shutdown() override;

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    unsigned int vao_ = 0;
    unsigned int shaderProgram_ = 0;
};
