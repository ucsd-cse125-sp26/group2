#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "ecs/Components.hpp"
#include "game/Physics.hpp"
#include "game/World.hpp"
#include "renderer/GpuTypes.hpp"
#include "renderer/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr float k_mouseSensitivity = 0.0015f; // radians per pixel
constexpr float k_fovDeg           = 100.0f;
constexpr float k_nearPlane        = 4.0f;    // Quake near (4 qu ≈ 12 cm)
constexpr float k_farPlane         = 16000.0f;

// Player spawn: standing on the floor (floor top = y 0, player half-height = 36)
constexpr glm::vec3 k_spawnPos = {0.0f, 36.0f, 0.0f};

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

struct AppState
{
    SDL_Window* window = nullptr;
    SDL_GPUDevice* gpu = nullptr;
    Renderer renderer;
    World world;
    PhysicsConfig physicsCfg;
    entt::registry registry;
    entt::entity player = entt::null;

    bool mouseCaptured  = false;
    bool prevJump       = false;
    uint32_t frameCount = 0;

    Uint64 lastTick = 0;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static glm::mat4 buildViewMatrix(const Transform& tf, const PlayerController& ctrl, const CameraAngles& cam)
{
    glm::vec3 eyePos = tf.position + glm::vec3(0.0f, ctrl.eyeHeight, 0.0f);

    float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
    float cy = std::cos(cam.yaw), sy = std::sin(cam.yaw);
    // Forward: forward in camera space
    glm::vec3 forward = {-sy * cp, sp, cy * cp};

    return glm::lookAt(eyePos, eyePos + forward, glm::vec3(0, 1, 0));
}

// ---------------------------------------------------------------------------
// SDL3 app callbacks
// ---------------------------------------------------------------------------

SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* /*argv*/[])
{
    SDL_SetAppMetadata("Titandoom", "0.1.0", "com.titandoom");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto* s   = new AppState();
    *appstate = s;

    // ---- GPU device ----
    s->gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    if (!s->gpu) {
        SDL_Log("GPU: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("GPU driver: %s", SDL_GetGPUDeviceDriver(s->gpu));

    // ---- Window ----
    // Fixed-size window — critical for Wayland compositors (Hyprland, etc.).
    // Setting min == max tells the compositor the window cannot be resized,
    // which causes Hyprland to create a floating window instead of tiling it.
    // Tiling causes a storm of configure events that keep the Vulkan swapchain
    // in perpetual recreation (SDL_WaitAndAcquireGPUSwapchainTexture → NULL).
    constexpr int k_winW = 1280, k_winH = 720;
    s->window = SDL_CreateWindow("Titandoom — bhop / walljump / glide demo", k_winW, k_winH, 0);
    if (!s->window) {
        SDL_Log("Window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetWindowResizable(s->window, false);
    SDL_SetWindowMinimumSize(s->window, k_winW, k_winH);
    SDL_SetWindowMaximumSize(s->window, k_winW, k_winH);

    if (!SDL_ClaimWindowForGPUDevice(s->gpu, s->window)) {
        SDL_Log("ClaimWindow: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Wait for the compositor to acknowledge the window size before we start
    // rendering.  On Wayland this resolves the size negotiation handshake so
    // the first swapchain acquire gets the correct dimensions.
    SDL_SyncWindow(s->window);
    {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(s->window, &pw, &ph);
        SDL_Log("Window pixel size after sync: %d×%d", pw, ph);
    }

    // ---- World ----
    s->world = level::build();

    // ---- Renderer ----
    if (!s->renderer.init(s->gpu, s->window, s->world))
        return SDL_APP_FAILURE;

    // ---- ECS: create the player entity ----
    s->player = s->registry.create();
    s->registry.emplace<Transform>(s->player, Transform{k_spawnPos});
    s->registry.emplace<Velocity>(s->player);
    s->registry.emplace<CameraAngles>(s->player);
    s->registry.emplace<InputState>(s->player);
    s->registry.emplace<PlayerController>(s->player);

    // ---- Capture mouse ----
    SDL_SetWindowRelativeMouseMode(s->window, true);
    s->mouseCaptured = true;

    s->lastTick = SDL_GetTicksNS();

    SDL_Log("Controls:");
    SDL_Log("  WASD = move   |  Mouse = look");
    SDL_Log("  Space = jump / bhop / walljump (hold for glide in air)");
    SDL_Log("  Ctrl  = crouch (future)");
    SDL_Log("  Esc   = toggle mouse capture");
    SDL_Log(" ");
    SDL_Log("Physics constants (Quake units):");
    SDL_Log("  Max ground speed : %g qu/s", static_cast<double>(s->physicsCfg.maxSpeedGround));
    SDL_Log("  Jump speed        : %g qu/s", static_cast<double>(s->physicsCfg.jumpSpeed));
    SDL_Log("  Gravity           : 800 qu/s^2");
    SDL_Log("  Auto-bhop         : %s", s->physicsCfg.autoBhop ? "ON" : "OFF");
    SDL_Log("---");

    return SDL_APP_CONTINUE;
}

// ---------------------------------------------------------------------------

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    auto* s = static_cast<AppState*>(appstate);

    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_KEY_DOWN:
        if (event->key.key == SDLK_ESCAPE) {
            s->mouseCaptured = !s->mouseCaptured;
            SDL_SetWindowRelativeMouseMode(s->window, s->mouseCaptured);
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!s->mouseCaptured) {
            s->mouseCaptured = true;
            SDL_SetWindowRelativeMouseMode(s->window, true);
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (s->mouseCaptured) {
            auto& cam = s->registry.get<CameraAngles>(s->player);
            cam.yaw += event->motion.xrel * k_mouseSensitivity;
            cam.pitch = std::clamp(
                cam.pitch - event->motion.yrel * k_mouseSensitivity, -glm::radians(89.0f), glm::radians(89.0f));
        }
        break;

    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

// ---------------------------------------------------------------------------

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto* s = static_cast<AppState*>(appstate);

    // ---- Delta time (capped at ~33 ms to avoid physics explosions) ----
    Uint64 now  = SDL_GetTicksNS();
    float dt    = static_cast<float>(now - s->lastTick) * 1e-9f;
    dt          = std::min(dt, 0.033f);
    s->lastTick = now;

    // ---- Build input snapshot from keyboard state ----
    if (s->mouseCaptured) {
        auto& inp        = s->registry.get<InputState>(s->player);
        const bool* keys = SDL_GetKeyboardState(nullptr);

        float mx       = static_cast<float>((keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0));
        float mz       = static_cast<float>((keys[SDL_SCANCODE_W] ? 1 : 0) - (keys[SDL_SCANCODE_S] ? 1 : 0));
        float len      = std::sqrt(mx * mx + mz * mz);
        inp.moveDir    = (len > 0.001f) ? glm::vec2(mx / len, mz / len) : glm::vec2(0.0f);
        inp.mouseDelta = {0.0f, 0.0f}; // already handled in SDL_EVENT_MOUSE_MOTION

        bool curJump    = keys[SDL_SCANCODE_SPACE] != 0;
        inp.jumpHeld    = curJump;
        inp.jumpPressed = curJump && !s->prevJump; // rising edge
        s->prevJump     = curJump;

        inp.glideHeld  = curJump; // same key: hold jump in air to glide
        inp.crouchHeld = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
    }

    // ---- Physics tick ----
    physicsUpdate(s->registry, s->world, s->physicsCfg, dt);

    // ---- Debug HUD (title bar) ----
    {
        auto& vel  = s->registry.get<Velocity>(s->player);
        auto& ctrl = s->registry.get<PlayerController>(s->player);
        float hspd = glm::length(glm::vec2(vel.linear.x, vel.linear.z));
        char title[128];
        std::snprintf(title,
                      sizeof(title),
                      "Titandoom | spd %.0f qu/s | vz %.0f | %s%s%s",
                      static_cast<double>(hspd),
                      static_cast<double>(vel.linear.y),
                      ctrl.onGround ? "GROUND " : "AIR ",
                      ctrl.onWall ? "WALL " : "",
                      ctrl.gliding ? "GLIDE " : "");
        SDL_SetWindowTitle(s->window, title);
    }

    // ---- Render ----
    SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(s->gpu);
    if (!cmdbuf)
        return SDL_APP_CONTINUE;

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 swW = 0, swH = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdbuf, s->window, &swapchain, &swW, &swH)) {
        SDL_CancelGPUCommandBuffer(cmdbuf);
        return SDL_APP_CONTINUE;
    }

    if (swapchain && swW > 0 && swH > 0) {
        s->frameCount++;
        if (s->frameCount == 1 || s->frameCount % 300 == 0) {
            SDL_Log(
                "frame %u  swapchain %u×%u  depth %s", s->frameCount, swW, swH, s->renderer.depthTex ? "OK" : "NULL");
        }
        // Rebuild depth texture if swapchain size changed
        s->renderer.onResize(swW, swH);

        const auto& tf   = s->registry.get<Transform>(s->player);
        const auto& ctrl = s->registry.get<PlayerController>(s->player);
        const auto& cam  = s->registry.get<CameraAngles>(s->player);

        glm::mat4 view = buildViewMatrix(tf, ctrl, cam);
        glm::mat4 proj = glm::perspective(
            glm::radians(k_fovDeg), static_cast<float>(swW) / static_cast<float>(swH), k_nearPlane, k_farPlane);
        // SDL3 Vulkan backend sets viewport.height = -h, which already flips Y.
        // Do NOT manually flip proj[1][1] — that would double-flip and invert the scene.

        SceneUniforms uniforms;
        uniforms.viewProj = proj * view;
        uniforms.model    = glm::mat4(1.0f);

        s->renderer.drawWorld(cmdbuf, swapchain, uniforms);
    }

    SDL_SubmitGPUCommandBuffer(cmdbuf);
    return SDL_APP_CONTINUE;
}

// ---------------------------------------------------------------------------

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    auto* s = static_cast<AppState*>(appstate);
    if (s) {
        s->renderer.destroy();
        if (s->gpu && s->window)
            SDL_ReleaseWindowFromGPUDevice(s->gpu, s->window);
        SDL_DestroyWindow(s->window);
        SDL_DestroyGPUDevice(s->gpu);
        delete s;
    }
    SDL_Quit();
}
