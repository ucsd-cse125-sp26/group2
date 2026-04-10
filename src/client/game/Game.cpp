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
#include <cstdio>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
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
    statsPrevTime = prevTime;

    // Apply the default VSync setting now that the renderer is ready.
    renderer.setVSync(limitFPSToMonitor);

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

        // R — toggle frame recording (state CSV + per-frame PNG screenshots).
        // Output lands in <binary_dir>/recordings/<timestamp>/ next to the game.
        case SDLK_R: {
            if (recorder.isRecording()) {
                recorder.stopRecording();
                SDL_Log("[client] recording stopped");
            } else {
                const char* base = SDL_GetBasePath();
                std::string baseDir = std::string(base ? base : "") + "recordings";
                recorder.startRecording(baseDir);
                SDL_Log("[client] recording started → %s", recorder.sessionDir().c_str());
            }
            break;
        }

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

// ═══════════════════════════════════════════════════════════════════════════
// iterate() — decoupled physics / render loop.
//
// Physics runs at a fixed 128 Hz behind an accumulator gate.  Up to
// k_maxTicksPerFrame ticks are processed per call so physics catches up
// even when the OS only calls iterate() at monitor refresh rate (e.g. 60 Hz).
//
// Three ImGui-tunable flags:
//
//   renderSeparateFromPhysics — render every iterate() call with position
//       interpolated between the last two physics ticks (true, default) vs.
//       render only after a physics tick (false, caps fps at 128 Hz).
//
//   inputSyncedWithPhysics — sample SDL mouse once per physics frame-group
//       so yaw and position share the same timebase (true, default, no jitter)
//       vs. sample every iterate() call (false, yaw at frame rate).
//
//   limitFPSToMonitor — VSync on (true) / off (false, default).
// ═══════════════════════════════════════════════════════════════════════════

SDL_AppResult Game::iterate()
{
    // ── 1. Accumulate real elapsed time ──────────────────────────────────
    const Uint64 k_perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 k_now = SDL_GetPerformanceCounter();

    float frameTime = static_cast<float>(k_now - prevTime) / static_cast<float>(k_perfFreq);
    frameTime = std::min(frameTime, 0.25f); // cap to avoid spiral-of-death
    prevTime = k_now;
    accumulator += frameTime;

    // ── 2. Refresh performance stats every 0.5 s ─────────────────────────
    static constexpr float k_statsPeriod = 0.5f;
    const float statsDt = static_cast<float>(k_now - statsPrevTime) / static_cast<float>(k_perfFreq);
    if (statsDt >= k_statsPeriod && fpsHistoryCount > 0) {
        // Physics rate: tick count / elapsed.
        measuredPhysicsHz = static_cast<float>(statsPhysTicks) / statsDt;
        statsPhysTicks = 0;
        statsPrevTime = k_now;

        // FPS percentile stats from the ring buffer.
        const int count = fpsHistoryCount; // may be < k_fpsHistorySize
        float sorted[k_fpsHistorySize];
        if (count < k_fpsHistorySize) {
            for (int i = 0; i < count; ++i)
                sorted[i] = fpsHistory[i];
        } else {
            // Full ring: oldest sample is at fpsHistoryHead.
            for (int i = 0; i < k_fpsHistorySize; ++i)
                sorted[i] = fpsHistory[(fpsHistoryHead + i) % k_fpsHistorySize];
        }
        std::sort(sorted, sorted + count); // ascending: worst fps first

        statsFPSMin = sorted[0];
        statsFPSMax = sorted[count - 1];
        statsFPS1pLow = sorted[static_cast<int>(static_cast<float>(count) * 0.01f)]; // 1st percentile
        statsFPS5pLow = sorted[static_cast<int>(static_cast<float>(count) * 0.05f)]; // 5th percentile
        // Most-recent sample (last written = head - 1).
        statsFPSCurrent = fpsHistory[(fpsHistoryHead - 1 + k_fpsHistorySize) % k_fpsHistorySize];
    }

    // ── 3. Input — sample every frame when NOT synced with physics ────────
    if (!inputSyncedWithPhysics && mouseCaptured)
        systems::runInputSample(registry, mouseSensitivity);

    // ── 4. Physics — run up to k_maxTicksPerFrame ticks to catch up ───────
    bool physicsRan = false;
    int ticksThisFrame = 0;

    if (accumulator >= k_physicsDt) {
        // Snapshot orientation BEFORE this frame's input so prevTickYaw holds
        // the end-of-previous-frame yaw — needed for correct view interpolation.
        registry.view<InputSnapshot, LocalPlayer>().each([](InputSnapshot& snap) {
            snap.prevTickYaw = snap.yaw;
            snap.prevTickPitch = snap.pitch;
        });

        // Input synced with physics: sample once for this whole group of ticks.
        if (inputSyncedWithPhysics && mouseCaptured)
            systems::runInputSample(registry, mouseSensitivity);

        while (accumulator >= k_physicsDt && ticksThisFrame < k_maxTicksPerFrame) {
            accumulator -= k_physicsDt;

            // Snapshot position before each tick so the last tick's delta is
            // available for interpolation (prevPos → pos over alpha ∈ [0,1]).
            registry.view<Position, PreviousPosition>().each(
                [](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });

            systems::runMovement(registry, k_physicsDt);
            systems::runCollision(registry, k_physicsDt, k_worldPlanes);
            ++tickCount;
            ++ticksThisFrame;
            ++statsPhysTicks;
        }

        while (client.poll()) {
        }

        physicsRan = true;
    }

    // ── 5. Bail out early if there is nothing new to render ───────────────
    if (!renderSeparateFromPhysics && !physicsRan)
        return SDL_APP_CONTINUE;

    // ── 6. Resolve camera ─────────────────────────────────────────────────
    glm::vec3 renderEye{0.0f, 100.0f, 0.0f};
    float renderYaw = 0.0f;
    float renderPitch = 0.0f;

    if (renderSeparateFromPhysics) {
        // Interpolation alpha: 0 = just ran a tick, approaching 1 as next tick nears.
        const float alpha = std::clamp(accumulator / k_physicsDt, 0.0f, 1.0f);

        registry.view<LocalPlayer, Position, PreviousPosition, InputSnapshot, CollisionShape>().each(
            [&](const Position& pos,
                const PreviousPosition& prev,
                const InputSnapshot& input,
                const CollisionShape& shape) {
                const glm::vec3 interpPos = glm::mix(prev.value, pos.value, alpha);
                const float eyeOffset = shape.halfExtents.y * 0.77f;
                renderEye = interpPos + glm::vec3{0.0f, eyeOffset, 0.0f};

                if (inputSyncedWithPhysics) {
                    // Yaw changes only at tick boundaries — lerp across the sub-tick gap.
                    renderYaw = input.prevTickYaw + (input.yaw - input.prevTickYaw) * alpha;
                    renderPitch = input.prevTickPitch + (input.pitch - input.prevTickPitch) * alpha;
                } else {
                    // Yaw already updated every frame — use directly.
                    renderYaw = input.yaw;
                    renderPitch = input.pitch;
                }
            });
    } else {
        // Sequential mode: use post-tick state directly (no interpolation).
        registry.view<LocalPlayer, Position, InputSnapshot, CollisionShape>().each(
            [&](const Position& pos, const InputSnapshot& input, const CollisionShape& shape) {
                const float eyeOffset = shape.halfExtents.y * 0.77f;
                renderEye = pos.value + glm::vec3{0.0f, eyeOffset, 0.0f};
                renderYaw = input.yaw;
                renderPitch = input.pitch;
            });
    }

    // ── 7. Frame recording (R key) — anchored to physics ticks ───────────
    if (physicsRan && recorder.isRecording()) {
        FrameState state;
        state.frameNumber = frameCount;
        state.timestamp = static_cast<double>(SDL_GetTicks()) / 1000.0 - recorder.startTimeSecs();
        state.tickCount = tickCount;
        state.renderEye = renderEye;
        state.renderYaw = renderYaw;
        state.renderPitch = renderPitch;

        registry.view<LocalPlayer, Position, Velocity, InputSnapshot>().each(
            [&](const Position& pos, const Velocity& vel, const InputSnapshot& input) {
                state.physPos = pos.value;
                state.physVel = vel.value;
                state.yaw = input.yaw;
                state.pitch = input.pitch;
            });

        int winW = 0, winH = 0;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        const float winWf = static_cast<float>(winW);
        const float winHf = static_cast<float>(winH);

        const float cosPitch = std::cos(renderPitch);
        const glm::vec3 fwd{std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
        const glm::mat4 view = glm::lookAt(renderEye, renderEye + fwd, glm::vec3{0, 1, 0});
        const glm::mat4 proj =
            glm::perspective(glm::radians(60.0f), (winHf > 0.0f) ? winWf / winHf : 1.0f, 5.0f, 15000.0f);
        const glm::mat4 vp = proj * view;

        const auto toScreen = [&](glm::vec3 p) -> glm::vec2 {
            const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
            if (clip.w <= 0.0f)
                return {-1.0f, -1.0f};
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return {(ndc.x * 0.5f + 0.5f) * winWf, (1.0f - (ndc.y * 0.5f + 0.5f)) * winHf};
        };
        state.cubeScreen = toScreen(glm::vec3{0.0f, 32.0f, 400.0f});
        state.modelScreen = toScreen(glm::vec3{200.0f, 0.0f, 400.0f});

        char capPath[512];
        std::snprintf(capPath,
                      sizeof(capPath),
                      "%s/frame_%06llu.png",
                      recorder.sessionDir().c_str(),
                      static_cast<unsigned long long>(frameCount));
        state.screenshotPath = capPath;
        renderer.requestScreenshot(capPath);

        recorder.recordFrame(state);
    }

    // ── 8. FPS sample — record inter-render delta into ring buffer ────────
    if (prevRenderTime != 0) {
        const float renderDt = static_cast<float>(k_now - prevRenderTime) / static_cast<float>(k_perfFreq);
        if (renderDt > 0.0f && renderDt < 1.0f) { // ignore startup / minimised outliers
            fpsHistory[fpsHistoryHead] = 1.0f / renderDt;
            fpsHistoryHead = (fpsHistoryHead + 1) % k_fpsHistorySize;
            if (fpsHistoryCount < k_fpsHistorySize)
                ++fpsHistoryCount;
        }
    }
    prevRenderTime = k_now;

    ++frameCount;

    // ── 9. VSync toggle — apply when limitFPSToMonitor changes ───────────
    // buildUI may modify limitFPSToMonitor, so we snapshot it before and
    // call setVSync only when it actually flips (avoids per-frame API calls).
    const bool prevLimitFPS = limitFPSToMonitor;

    // ── 10. Render ────────────────────────────────────────────────────────
    debugUI.newFrame();
    debugUI.buildUI(registry,
                    tickCount,
                    mouseSensitivity,
                    renderSeparateFromPhysics,
                    inputSyncedWithPhysics,
                    limitFPSToMonitor,
                    measuredPhysicsHz,
                    statsFPSCurrent,
                    statsFPSMin,
                    statsFPSMax,
                    statsFPS1pLow,
                    statsFPS5pLow);
    debugUI.render();
    renderer.drawFrame(renderEye, renderYaw, renderPitch);

    if (limitFPSToMonitor != prevLimitFPS)
        renderer.setVSync(limitFPSToMonitor);

    return SDL_APP_CONTINUE;
}

void Game::quit()
{
    if (recorder.isRecording())
        recorder.stopRecording();
    renderer.quit();
    debugUI.shutdown();
    client.shutdown();
    SDL_DestroyWindow(window);
    NET_Quit();
    SDL_Quit();
}
