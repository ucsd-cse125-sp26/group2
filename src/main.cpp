#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Returns the path to a compiled shader (.spv) file relative to the binary.
// Looks inside a "shaders/" subdirectory next to the executable.
static std::string shaderPath(const char* filename)
{
    const char* base = SDL_GetBasePath();
    std::string path = base ? base : "./";
    path += "shaders/";
    path += filename;
    return path;
}

static SDL_GPUShader* loadShader(SDL_GPUDevice* gpu,
                                 const char* filename,
                                 SDL_GPUShaderStage stage,
                                 Uint32 samplerCount,
                                 Uint32 uniformBufferCount,
                                 Uint32 storageBufferCount,
                                 Uint32 storageTextureCount)
{
    std::string path = shaderPath(filename);
    size_t codeSize  = 0;
    void* code       = SDL_LoadFile(path.c_str(), &codeSize);
    if (!code) {
        SDL_Log("Failed to load shader %s: %s", path.c_str(), SDL_GetError());
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info{};
    info.code                 = static_cast<const Uint8*>(code);
    info.code_size            = codeSize;
    info.entrypoint           = "main";
    info.format               = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage                = stage;
    info.num_samplers         = samplerCount;
    info.num_uniform_buffers  = uniformBufferCount;
    info.num_storage_buffers  = storageBufferCount;
    info.num_storage_textures = storageTextureCount;

    SDL_GPUShader* shader = SDL_CreateGPUShader(gpu, &info);
    SDL_free(code);
    return shader;
}

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

struct AppState
{
    SDL_Window* window                = nullptr;
    SDL_GPUDevice* gpu                = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    entt::registry registry;
};

// ---------------------------------------------------------------------------
// Lifecycle callbacks
// ---------------------------------------------------------------------------

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    SDL_SetAppMetadata("Titandoom", "0.1.0", "com.titandoom");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto* state = new AppState();
    *appstate   = state;

    state->gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV,
                                     /*debug_mode=*/true,
                                     /*name=*/nullptr);
    if (!state->gpu) {
        SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("GPU driver: %s", SDL_GetGPUDeviceDriver(state->gpu));

    state->window = SDL_CreateWindow("Titandoom", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!state->window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_ClaimWindowForGPUDevice(state->gpu, state->window)) {
        SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // ---- Load shaders ---------------------------------------------------
    SDL_GPUShader* vert = loadShader(state->gpu, "triangle.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0, 0);
    SDL_GPUShader* frag = loadShader(state->gpu, "triangle.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0);
    if (!vert || !frag) {
        return SDL_APP_FAILURE;
    }

    // ---- Build graphics pipeline ----------------------------------------
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(state->gpu, state->window);

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader                         = vert;
    pipelineInfo.fragment_shader                       = frag;
    pipelineInfo.primitive_type                        = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.target_info.color_target_descriptions = &colorTarget;
    pipelineInfo.target_info.num_color_targets         = 1;
    pipelineInfo.rasterizer_state.fill_mode            = SDL_GPU_FILLMODE_FILL;
    pipelineInfo.rasterizer_state.cull_mode            = SDL_GPU_CULLMODE_NONE;
    pipelineInfo.rasterizer_state.front_face           = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

    state->pipeline = SDL_CreateGPUGraphicsPipeline(state->gpu, &pipelineInfo);

    // Shaders are referenced by the pipeline — release our handles
    SDL_ReleaseGPUShader(state->gpu, vert);
    SDL_ReleaseGPUShader(state->gpu, frag);

    if (!state->pipeline) {
        SDL_Log("SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

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
        SDL_GPUColorTargetInfo color{};
        color.texture     = swapchain;
        color.clear_color = {0.08f, 0.08f, 0.12f, 1.0f};
        color.load_op     = SDL_GPU_LOADOP_CLEAR;
        color.store_op    = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmdbuf, &color, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(pass, state->pipeline);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }

    SDL_SubmitGPUCommandBuffer(cmdbuf);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    auto* state = static_cast<AppState*>(appstate);
    if (state) {
        if (state->pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(state->gpu, state->pipeline);
        }
        if (state->gpu && state->window) {
            SDL_ReleaseWindowFromGPUDevice(state->gpu, state->window);
        }
        SDL_DestroyWindow(state->window);
        SDL_DestroyGPUDevice(state->gpu);
        delete state;
    }
    SDL_Quit();
}
