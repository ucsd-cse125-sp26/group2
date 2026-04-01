#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

struct AppState
{
    SDL_Window* window = nullptr;
    SDL_GPUDevice* gpu = nullptr;
    entt::registry registry;
};

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    SDL_SetAppMetadata("Titandoom", "0.1.0", "com.titandoom");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto* state = new AppState();
    *appstate   = state;

    state->gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL,
                                     /*debug_mode=*/true,
                                     /*name=*/nullptr);
    if (!state->gpu) {
        SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->window = SDL_CreateWindow("Titandoom", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!state->window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_ClaimWindowForGPUDevice(state->gpu, state->window)) {
        SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_Log("GPU driver: %s", SDL_GetGPUDeviceDriver(state->gpu));
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto* state = static_cast<AppState*>(appstate);

    SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(state->gpu);
    if (!cmdbuf) {
        return SDL_APP_CONTINUE;
    }

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdbuf, state->window, &swapchain, &w, &h)) {
        SDL_CancelGPUCommandBuffer(cmdbuf);
        return SDL_APP_CONTINUE;
    }

    if (swapchain) {
        SDL_GPUColorTargetInfo color_target{};
        color_target.texture     = swapchain;
        color_target.clear_color = {0.08f, 0.08f, 0.12f, 1.0f};
        color_target.load_op     = SDL_GPU_LOADOP_CLEAR;
        color_target.store_op    = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmdbuf, &color_target, 1, nullptr);
        SDL_EndGPURenderPass(pass);
    }

    SDL_SubmitGPUCommandBuffer(cmdbuf);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    auto* state = static_cast<AppState*>(appstate);
    if (state) {
        if (state->gpu && state->window) {
            SDL_ReleaseWindowFromGPUDevice(state->gpu, state->window);
        }
        SDL_DestroyWindow(state->window);
        SDL_DestroyGPUDevice(state->gpu);
        delete state;
    }
    SDL_Quit();
}
