#pragma once
#include "IRenderer.hpp"

#include <SDL3/SDL.h>

class OpenGLRenderer : public IRenderer
{
public:
    bool init(SDL_Window* window) override;
    void draw(SDL_GPURenderPass* /*unused*/) override;
    [[nodiscard]] SDL_GPUDevice* device() const override { return nullptr; }
    void shutdown() override;

private:
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    unsigned int vao = 0;
    unsigned int shaderProgram = 0;
};
