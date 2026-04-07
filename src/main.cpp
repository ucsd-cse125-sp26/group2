#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <string>

#ifdef USE_OPENGL
#include "renderer/OpenGLRenderer.hpp"
using ActiveRenderer = OpenGLRenderer;
#else
#include "renderer/SDLGPURenderer.hpp"
using ActiveRenderer = SDLGPURenderer;
#endif

#include "ecs/Registry.hpp"
#include "ui/imgui_layer.hpp"

#ifndef USE_OPENGL
#include "ui/sdl3gpu_driver.hpp"
#include "ui/ultralight_layer.hpp"
#endif

#ifndef NDEBUG
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#endif

// ---------------------------------------------------------------------------
// ECS components (demo)
// ---------------------------------------------------------------------------
struct Position
{
    float x = 0.f;
    float y = 0.f;
};
struct Velocity
{
    float dx = 0.f;
    float dy = 0.f;
};

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------
struct AppState
{
    SDL_Window* window = nullptr;
    ActiveRenderer renderer;
    Registry registry;
    ImGuiLayer imgui;
#ifndef USE_OPENGL
    SDL3GPUDriver* ulDriver = nullptr;
    UltralightLayer ultralightLayer{nullptr};
#endif
    uint64_t lastTicks = 0;
    int frameCount = 0;
    uint64_t lastStatusTick = 0;
};

// ---------------------------------------------------------------------------
// SDL3 app callbacks
// ---------------------------------------------------------------------------
SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* /*argv*/[])
{
    SDL_SetAppMetadata("group2", "0.1.0", "com.cse125.group2");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

#ifdef USE_OPENGL
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif

    auto* s = new AppState();
    *appstate = s;

    constexpr int k_winW = 1280;
    constexpr int k_winH = 720;

#ifdef USE_OPENGL
    constexpr SDL_WindowFlags k_winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
#else
    constexpr SDL_WindowFlags k_winFlags = SDL_WINDOW_RESIZABLE;
#endif

    s->window = SDL_CreateWindow("group2", k_winW, k_winH, k_winFlags);
    if (!s->window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!s->renderer.init(s->window)) {
        SDL_Log("Renderer init failed");
        return SDL_APP_FAILURE;
    }

    if (!s->imgui.init(s->window, s->renderer.device())) {
        SDL_Log("ImGui init failed");
        return SDL_APP_FAILURE;
    }

#ifndef USE_OPENGL
    s->ulDriver = new SDL3GPUDriver(s->renderer.device());
    new (&s->ultralightLayer) UltralightLayer(s->ulDriver);

    const std::string k_basePath = SDL_GetBasePath() ? SDL_GetBasePath() : "./";
    if (!s->ulDriver->buildPipelines(k_basePath.c_str(), s->window))
        SDL_Log("UL pipeline build failed — UI disabled");

    if (!s->ultralightLayer.init(s->window, s->renderer.device())) {
        SDL_Log("UltralightLayer init failed");
        return SDL_APP_FAILURE;
    }

    // Handle menu actions forwarded from JS via window.onAction().
    s->ultralightLayer.setActionCallback([](const std::string& action) {
        SDL_Log("[App] JS action: \"%s\"", action.c_str());
        if (action == "quit") {
            SDL_Event quitEvt{};
            quitEvt.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quitEvt);
        }
        // "new_game" and "settings" can be wired up as the game grows.
    });
#endif

    // ---- EnTT demo entities ----
    auto& reg = s->registry;
    auto e0 = reg.create();
    reg.emplace<Position>(e0, 640.f, 360.f);
    reg.emplace<Velocity>(e0, 50.f, 30.f);
    auto e1 = reg.create();
    reg.emplace<Position>(e1, 200.f, 150.f);
    reg.emplace<Velocity>(e1, -30.f, 20.f);
    auto e2 = reg.create();
    reg.emplace<Position>(e2, 900.f, 500.f);
    reg.emplace<Velocity>(e2, 10.f, -40.f);
    SDL_Log("[EnTT] alive=%zu", static_cast<size_t>(reg.storage<entt::entity>().size()));

#ifdef USE_OPENGL
    SDL_Log("Backend: OpenGL 4.1 core");
#else
    SDL_Log("Backend: SDL3 GPU pipeline");
#endif

    s->lastTicks = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    auto* s = static_cast<AppState*>(appstate);
#ifndef NDEBUG
    ImGui_ImplSDL3_ProcessEvent(event);
#endif
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        return SDL_APP_SUCCESS;

#ifndef USE_OPENGL
    // Forward mouse/scroll events into the Ultralight view so JS click
    // handlers and hover states work correctly.
    switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION:
        s->ultralightLayer.fireMouseMove(static_cast<int>(event->motion.x), static_cast<int>(event->motion.y));
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        s->ultralightLayer.fireMouseButton(static_cast<int>(event->button.x),
                                           static_cast<int>(event->button.y),
                                           true,
                                           event->button.button == SDL_BUTTON_RIGHT);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        s->ultralightLayer.fireMouseButton(static_cast<int>(event->button.x),
                                           static_cast<int>(event->button.y),
                                           false,
                                           event->button.button == SDL_BUTTON_RIGHT);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        s->ultralightLayer.fireScroll(
            0, 0, static_cast<int>(event->wheel.x * 20.f), static_cast<int>(event->wheel.y * 20.f));
        break;
    default:
        break;
    }
#endif

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto* s = static_cast<AppState*>(appstate);
    auto& reg = s->registry;

    // ---- Delta time ----
    const uint64_t k_now = SDL_GetTicks();
    const float k_dt = static_cast<float>(k_now - s->lastTicks) / 1000.f;
    s->lastTicks = k_now;

    // ---- EnTT system: move entities ----
    reg.view<Position, Velocity>().each([k_dt](Position& p, const Velocity& v) {
        p.x += v.dx * k_dt;
        p.y += v.dy * k_dt;
    });

    // ---- ImGui new-frame ----
    s->imgui.newFrame();
#ifndef NDEBUG
    ImGui::Begin("Debug");
    ImGui::Text("Backend: %s",
#ifdef USE_OPENGL
                "OpenGL 4.1"
#else
                "SDL3 GPU"
#endif
    );
    ImGui::Text("Entities: %zu", static_cast<size_t>(reg.storage<entt::entity>().size()));
    ImGui::Text("FPS:      %.1f", static_cast<double>(ImGui::GetIO().Framerate));
    ImGui::Text("UL:       active");
    ImGui::End();
#endif

#ifdef USE_OPENGL
    // OpenGL path: finalise ImGui frame, draw triangle, render ImGui overlay, then swap.
    s->imgui.endFrame();
    s->renderer.draw(nullptr);         // clear + draw triangle (no swap)
    s->imgui.render(nullptr, nullptr); // draw ImGui overlay on top
    SDL_GL_SwapWindow(s->window);      // now swap the complete frame
#else
    // SDL3 GPU path: shared command buffer.
    // endFrame() must be called every frame to keep ImGui's internal
    // frame counter consistent, even when the swapchain is unavailable.
#ifndef USE_OPENGL
    s->ultralightLayer.update();
    s->ultralightLayer.render();

    // Push a live frame-count to the HTML footer once per second.
    ++s->frameCount;
    if (k_now - s->lastStatusTick >= 1000u) {
        s->ultralightLayer.pushFrameCount(s->frameCount);
        s->frameCount = 0;
        s->lastStatusTick = k_now;
    }
#endif
    s->imgui.endFrame();

    SDL_GPUDevice* dev = s->renderer.device();

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd)
        return SDL_APP_CONTINUE;

    SDL_GPUTexture* swapchain = nullptr;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, s->window, &swapchain, nullptr, nullptr) || !swapchain) {
        SDL_CancelGPUCommandBuffer(cmd);
        return SDL_APP_CONTINUE;
    }

    // Upload ImGui vertex/index data BEFORE SDL_BeginGPURenderPass.
    s->imgui.prepareGPU(cmd);
#ifndef USE_OPENGL
    s->ulDriver->flushCommands(cmd);
#endif

    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = swapchain;
    colorTarget.clear_color = {.r = 0.1f, .g = 0.1f, .b = 0.1f, .a = 1.f};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, nullptr);

    s->renderer.draw(pass);
#ifndef USE_OPENGL
    {
        int vpW = 0, vpH = 0;
        SDL_GetWindowSize(s->window, &vpW, &vpH);
        s->ultralightLayer.composite(cmd, pass, static_cast<uint32_t>(vpW), static_cast<uint32_t>(vpH));
    }
#endif
    s->imgui.render(pass, cmd);

    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
#endif

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    auto* s = static_cast<AppState*>(appstate);
    if (!s)
        return;
#ifndef USE_OPENGL
    s->ultralightLayer.shutdown();
    delete s->ulDriver;
    s->ulDriver = nullptr;
#endif
    s->imgui.shutdown();
    s->renderer.shutdown();
    SDL_DestroyWindow(s->window);
    SDL_Quit();
    delete s;
}
