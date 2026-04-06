#pragma once
#include <SDL3/SDL.h>

#include <Ultralight/Ultralight.h>

class SDL3GPUDriver;

class UltralightLayer
{
public:
    explicit UltralightLayer(SDL3GPUDriver* driver);
    ~UltralightLayer();

    bool init(SDL_Window* window, SDL_GPUDevice* device);
    void update();
    void render();
    void composite(SDL_GPURenderPass* pass, uint32_t viewportW, uint32_t viewportH);
    void shutdown();

private:
    SDL3GPUDriver* driver = nullptr;
    SDL_GPUDevice* gpuDevice = nullptr;
    ultralight::RefPtr<ultralight::Renderer> renderer;
    ultralight::RefPtr<ultralight::View> view;
};
