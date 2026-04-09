#include "Game.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/systems/CollisionSystem.hpp"
#include "ecs/systems/MovementSystem.hpp"
#include "systems/InputSampleSystem.hpp"

#include <SDL3/SDL_video.h>

#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <glm/glm.hpp>

// World geometry for the current test scene: a single floor plane at y=0.
// Will be replaced by a proper World object when map loading is implemented.
static const std::array k_worldPlanes{physics::Plane{.normal = glm::vec3{0.0f, 1.0f, 0.0f}, .distance = 0.0f}};

bool Game::init()
{
    SDL_SetAppMetadata("group2", "0.1.0", "com.cse125.group2");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if (!NET_Init()) {
        SDL_Log("NET_Init() failed: %s", SDL_GetError());
        return false;
    }

    window = SDL_CreateWindow("group2", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    // DebugUI must be initialised before Renderer — it creates the ImGui
    // context that the GPU render backend (in Renderer::init) requires.
    if (!debugUI.init(window)) {
        SDL_Log("DebugUI init failed");
        SDL_DestroyWindow(window);
        return false;
    }

    if (!renderer.init(window)) {
        SDL_Log("Renderer init failed");
        debugUI.shutdown();
        SDL_DestroyWindow(window);
        return false;
    }

    if (!client.init("127.0.0.1", 9999)) {
        SDL_Log("Failed to connect to server");
        renderer.quit();
        debugUI.shutdown();
        SDL_DestroyWindow(window);
        return false;
    }

    // Grab the mouse into relative mode so camera look works immediately.
    SDL_SetWindowRelativeMouseMode(window, true);
    mouseCaptured = true;

    // Spawn the local player entity with all physics and input components.
    const glm::vec3 k_startPos{0.0f, 200.0f, 0.0f};
    const entt::entity k_player = registry.create();
    registry.emplace<Position>(k_player, k_startPos);
    registry.emplace<PreviousPosition>(k_player, k_startPos);
    registry.emplace<Velocity>(k_player);
    registry.emplace<CollisionShape>(k_player);
    registry.emplace<PlayerState>(k_player);
    registry.emplace<InputSnapshot>(k_player);
    registry.emplace<LocalPlayer>(k_player);

    prevTime = SDL_GetPerformanceCounter();
    SDL_Log("[client] local player spawned at (0, 200, 0), physicsHz=%d", k_physicsHz);
    return true;
}

SDL_AppResult Game::event(SDL_Event* event)
{
    // Forward every event to ImGui first so it can capture keyboard/mouse
    // when the cursor is hovering over a window.
    debugUI.processEvent(event);

    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    if (event->type == SDL_EVENT_KEY_DOWN) {
        switch (event->key.key) {
        case SDLK_Q:
            return SDL_APP_SUCCESS;

        // ESC — toggle mouse capture so the player can reach the ImGui windows.
        case SDLK_ESCAPE:
            mouseCaptured = !mouseCaptured;
            SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
            break;

        // F1 — send a test hello packet to the server.
        case SDLK_F1: {
            static constexpr char k_helloMsg[] = "Hello from client!";
            client.send(k_helloMsg, static_cast<int>(sizeof(k_helloMsg) - 1));
            SDL_Log("Sent test packet to server");
            break;
        }

        default:
            break;
        }
    }

    // Re-capture mouse on window click while uncaptured (standard FPS behaviour).
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && !mouseCaptured) {
        mouseCaptured = true;
        SDL_SetWindowRelativeMouseMode(window, true);
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult Game::iterate()
{
    // ImGui frame start — must happen before any ImGui calls this frame.
    debugUI.newFrame();

    // Sample input once per frame before the physics loop.
    // Mouse deltas are consumed here; running inside the loop would
    // accumulate them multiple times per frame at high frame rates.
    if (mouseCaptured)
        systems::runInputSample(registry);

    // Compute frame time.
    const Uint64 k_perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 k_now = SDL_GetPerformanceCounter();

    float frameTime = static_cast<float>(k_now - prevTime) / static_cast<float>(k_perfFreq);
    prevTime = k_now;
    frameTime = std::min(frameTime, 0.25f); // clamp to avoid spiral-of-death
    accumulator += frameTime;

    // Fixed-step physics loop.
    while (accumulator >= k_physicsDt) {
        // Snapshot positions for sub-tick interpolation.
        registry.view<Position, PreviousPosition>().each(
            [](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });

        // Snapshot yaw/pitch for sub-tick interpolation.
        // Mirrors the PreviousPosition pattern: prevTickYaw/Pitch hold the
        // orientation at the START of this tick so the renderer can interpolate
        // orientation with the same alpha as position — keeping eye and look-
        // direction on the same timebase and eliminating strafe+rotate jitter.
        registry.view<InputSnapshot, LocalPlayer>().each([](InputSnapshot& snap) {
            snap.prevTickYaw = snap.yaw;
            snap.prevTickPitch = snap.pitch;
        });

        systems::runMovement(registry, k_physicsDt);
        systems::runCollision(registry, k_physicsDt, k_worldPlanes);

        accumulator -= k_physicsDt;
        ++tickCount;
    }

    // Network receive.
    while (client.poll()) {
    }

    // ── resolve first-person camera from local player ──────────────────────
    // Default eye position used before the player entity is available.
    glm::vec3 renderEye{0.0f, 100.0f, 0.0f};
    float renderYaw = 0.0f;
    float renderPitch = 0.0f;

    // ── velocity extrapolation ────────────────────────────────────────────────
    // Why not interpolation (mix(prev, pos, alpha))?
    //
    // Interpolation has a discontinuous jump of magnitude V*frameTime at every
    // physics-tick boundary.  Here's the math:
    //
    //   Frame N  (no tick fires): renderEye = mix(prev, pos, α₀)        e.g. α₀ = 0.64
    //   Frame N+1 (tick fires):   renderEye = mix(pos,  pos', α₁)       e.g. α₁ = 0.28
    //     jump = pos + α₁·Δ − (prev + α₀·Δ)  =  Δ·(1 + α₁ − α₀)  =  V·frameTime
    //
    // At 400 u/s strafe speed and 5 ms frames that's ~2 world-units sideways
    // every 7.8 ms (128 Hz physics tick) — clearly visible when orbiting because
    // the eye is expected to stay fixed relative to the object.
    //
    // Velocity extrapolation — renderEye(t) = pos + vel·(t − t_lastTick) — is a
    // continuous linear function of time.  Proof that it has no tick-boundary jump:
    //
    //   Before tick:  pos_T  + vel·acc
    //   After tick:   pos_T+1 + vel·(acc + frameTime − dt)
    //               = (pos_T + vel·dt) + vel·(acc + frameTime − dt)
    //               = pos_T + vel·acc + vel·frameTime   ← same up to vel·frameTime
    //
    // Both frames add exactly vel·frameTime, so the position advances smoothly and
    // there is no backwards snap.  The yaw is the frame's current value (no lag),
    // which is consistent: both eye and look-direction are at "now".
    registry.view<LocalPlayer, Position, Velocity, InputSnapshot, CollisionShape>().each(
        [&](const Position& pos, const Velocity& vel, const InputSnapshot& input, const CollisionShape& shape) {
            // Extrapolate position forward from last physics tick using current velocity.
            const glm::vec3 extrapolated = pos.value + vel.value * accumulator;

            // Eye sits at ~77 % of the AABB half-height above the centre
            // (≈ 64 units from feet for a standing player — standard FPS height).
            // This adapts automatically to crouching (halfExtents.y shrinks to 22).
            const float eyeOffset = shape.halfExtents.y * 0.77f;
            renderEye = extrapolated + glm::vec3{0.0f, eyeOffset, 0.0f};
            renderYaw = input.yaw;
            renderPitch = input.pitch;
        });

    // Build debug UI and render.
    debugUI.buildUI(registry, tickCount);
    debugUI.render();
    renderer.drawFrame(renderEye, renderYaw, renderPitch);

    return SDL_APP_CONTINUE;
}

void Game::quit()
{
    renderer.quit();
    debugUI.shutdown();
    client.shutdown();
    SDL_DestroyWindow(window);
    NET_Quit();
    SDL_Quit();
}
