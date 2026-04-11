#include "Game.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Renderable.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/systems/CollisionSystem.hpp"
#include "ecs/systems/MovementSystem.hpp"
#include "particles/ParticleEvents.hpp"
#include "systems/InputSampleSystem.hpp"

#include <SDL3/SDL_video.h>

#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <cstdio>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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

    // Particle system needs the device + formats from the renderer.
    // colorFmt must match the render target particles draw into (HDR = RGBA16F),
    // NOT the swapchain format.  shaderFmt must be the single format the
    // renderer selected, not the bitmask of all supported formats.
    if (!particleSystem.init(renderer.getDevice(), Renderer::getHdrFormat(), renderer.getShaderFormat())) {
        SDL_Log("ParticleSystem init failed (non-fatal — particles disabled)");
    } else {
        renderer.setParticleSystem(&particleSystem);

        // Wire dispatcher events to particle system
        dispatcher.sink<WeaponFiredEvent>().connect<&ParticleSystem::onWeaponFired>(particleSystem);
        dispatcher.sink<ProjectileImpactEvent>().connect<&ParticleSystem::onImpact>(particleSystem);
        dispatcher.sink<ExplosionEvent>().connect<&ParticleSystem::onExplosion>(particleSystem);
    }

    if (!client.init("127.0.0.1", 9999)) {
        SDL_Log("Failed to connect to server");
        particleSystem.quit();
        renderer.quit();
        debugUI.shutdown();
        SDL_DestroyWindow(window);
        return false;
    }

    // Grab the mouse into relative mode so camera look works immediately.
    SDL_SetWindowRelativeMouseMode(window, true);
    mouseCaptured = true;

    // ── Load models for entity rendering ────────────────────────────────────
    wraithModelIdx = renderer.loadSceneModel("Apex_Legend_Wraith.glb", glm::vec3(0.0f), 8.0f);
    if (wraithModelIdx < 0)
        SDL_Log("[client] WARNING: Wraith model failed to load — player model will be invisible");

    weaponModelIdx = renderer.loadSceneModel("r-301_-_apex_legends.glb", glm::vec3(0.0f), 1.0f);
    if (weaponModelIdx < 0)
        SDL_Log("[client] WARNING: R-301 model failed to load — weapon will be invisible");

    // ── Load animated model (Mixamo FBX) ──────────────────────────────────
    {
        const char* base = SDL_GetBasePath();
        std::string fbxPath = std::string(base ? base : "") + "assets/Standard_Run.fbx";
        if (runAnimation.load(fbxPath)) {
            animatedModelIdx = renderer.uploadSceneModel(runAnimation.getLoadedModel());
            if (animatedModelIdx >= 0) {
                SDL_Log("[client] animated model uploaded — index=%d, duration=%.2fs",
                        animatedModelIdx,
                        static_cast<double>(runAnimation.duration()));
            }
        } else {
            SDL_Log("[client] WARNING: animated model failed to load — animation disabled");
        }
    }

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
    registry.emplace<WeaponState>(k_player);

    // Attach a Renderable for the player model (Wraith).
    // The local player's model is rendered for other clients (skipped for self in 1P mode).
    if (wraithModelIdx >= 0)
        registry.emplace<Renderable>(k_player, Renderable{.modelIndex = wraithModelIdx, .scale = glm::vec3(8.0f)});

    // Spawn a visible animated character in the world (Mixamo run animation).
    if (animatedModelIdx >= 0) {
        const glm::vec3 animPos{0.0f, 0.0f, 400.0f};
        const entt::entity animEntity = registry.create();
        registry.emplace<Position>(animEntity, animPos);
        registry.emplace<PreviousPosition>(animEntity, animPos);
        registry.emplace<Renderable>(animEntity, Renderable{.modelIndex = animatedModelIdx, .scale = glm::vec3(1.0f)});
    }

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

        // ── Particle system test keys ───────────────────────────────────────
        case SDLK_T: {
            // Energy beam — hits floor or max range
            const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
            const glm::vec3 hip = cachedEye_ + right * 15.f - glm::vec3{0, 1, 0} * 8.f + cachedCamFwd_ * 5.f;
            float dist = 500.f;
            glm::vec3 hitN = -cachedCamFwd_;
            if (cachedCamFwd_.y < -0.001f) {
                const float t = -cachedEye_.y / cachedCamFwd_.y;
                if (t > 0.f && t < dist) {
                    dist = t;
                    hitN = {0, 1, 0};
                }
            }
            const glm::vec3 hitP = cachedEye_ + cachedCamFwd_ * dist;
            particleSystem.spawnHitscanBeam(hip, hitP, WeaponType::EnergyRifle);
            particleSystem.spawnImpactEffect(hitP, hitN, SurfaceType::Energy, WeaponType::EnergyRifle);
            break;
        }
        case SDLK_Y: {
            // Bullet tracer — hits floor or max range
            const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
            const glm::vec3 hip = cachedEye_ + right * 15.f - glm::vec3{0, 1, 0} * 8.f + cachedCamFwd_ * 5.f;
            float dist = 500.f;
            glm::vec3 hitN = -cachedCamFwd_;
            if (cachedCamFwd_.y < -0.001f) {
                const float t = -cachedEye_.y / cachedCamFwd_.y;
                if (t > 0.f && t < dist) {
                    dist = t;
                    hitN = {0, 1, 0};
                }
            }
            const glm::vec3 hitP = cachedEye_ + cachedCamFwd_ * dist;
            particleSystem.spawnBulletTracer(hip, cachedCamFwd_, dist);
            particleSystem.spawnImpactEffect(hitP, hitN, SurfaceType::Metal, WeaponType::Rifle);
            break;
        }
        case SDLK_U: {
            particleSystem.spawnSmoke(cachedEye_ + cachedCamFwd_ * 200.f, 40.f);
            break;
        }
        case SDLK_I: {
            particleSystem.spawnExplosion(cachedEye_ + cachedCamFwd_ * 300.f, 100.f);
            break;
        }
        case SDLK_O: {
            particleSystem.drawScreenText({10.f, 40.f}, "HP 100  AMMO 30", {1.f, 1.f, 1.f, 1.f}, 24.f);
            break;
        }

        default:
            break;
        }
    }

    // ── Left-click fires weapon (dispatches WeaponFiredEvent) ────────────
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT && mouseCaptured) {
        const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
        const glm::vec3 hip = cachedEye_ + right * 15.f - glm::vec3{0, 1, 0} * 8.f + cachedCamFwd_ * 5.f;

        // ── Ray-scene intersection for hit position ─────────────────────
        // Test against the floor plane (y=0) and use the nearest hit.
        constexpr float k_maxRange = 5000.f;
        float hitDist = k_maxRange; // fallback: max range
        glm::vec3 hitNormal = -cachedCamFwd_;
        SurfaceType hitSurface = SurfaceType::Concrete;

        // Floor plane: y = 0, normal = (0, 1, 0)
        if (cachedCamFwd_.y < -0.001f) { // looking downward at all
            const float t = -cachedEye_.y / cachedCamFwd_.y;
            if (t > 0.f && t < hitDist) {
                hitDist = t;
                hitNormal = glm::vec3{0.f, 1.f, 0.f};
                hitSurface = SurfaceType::Concrete;
            }
        }

        // Test against scene objects as rough spheres.
        struct SceneObj
        {
            glm::vec3 center;
            float radius;
            SurfaceType surface;
        };
        static const SceneObj k_sceneObjects[] = {
            {{200.f, 60.f, 400.f}, 80.f, SurfaceType::Flesh},   // Wraith
            {{-200.f, 40.f, 400.f}, 100.f, SurfaceType::Metal}, // Porsche
            {{0.f, 50.f, 600.f}, 60.f, SurfaceType::Metal},     // Pallet
            {{100.f, 30.f, 400.f}, 30.f, SurfaceType::Metal},   // Bottle
        };
        for (const auto& obj : k_sceneObjects) {
            // Ray-sphere intersection: |origin + t*dir - center|² = r²
            const glm::vec3 oc = cachedEye_ - obj.center;
            const float b = glm::dot(oc, cachedCamFwd_);
            const float c = glm::dot(oc, oc) - obj.radius * obj.radius;
            const float disc = b * b - c;
            if (disc >= 0.f) {
                const float t = -b - std::sqrt(disc);
                if (t > 0.f && t < hitDist) {
                    hitDist = t;
                    const glm::vec3 p = cachedEye_ + cachedCamFwd_ * t;
                    hitNormal = glm::normalize(p - obj.center);
                    hitSurface = obj.surface;
                }
            }
        }

        const glm::vec3 hitPos = cachedEye_ + cachedCamFwd_ * hitDist;
        const float tracerRange = hitDist; // tracer ends at the hit point

        WeaponFiredEvent wfe;
        wfe.type = WeaponType::Rifle;
        wfe.origin = hip;
        wfe.direction = cachedCamFwd_;
        wfe.isHitscan = true;
        wfe.hitPos = hitPos;
        dispatcher.enqueue(wfe);

        // Spawn tracers from muzzle to hit, impact at the hit surface.
        particleSystem.spawnBulletTracer(hip, cachedCamFwd_, tracerRange);
        particleSystem.spawnImpactEffect(hitPos, hitNormal, hitSurface, WeaponType::Rifle);
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
// Physics ALWAYS runs at exactly 128 Hz (k_physicsHz) using an accumulator
// with a multi-tick catch-up loop (up to k_maxTicksPerFrame per call).
// This is non-negotiable: it must match the server tick rate.
//
// Input is split into two independent streams:
//
//   Mouse look (yaw / pitch) — sampled EVERY iterate() call so camera
//       rotation is perfectly smooth at whatever frame rate the renderer
//       produces.  The camera always uses the latest yaw directly (never
//       interpolated).  Interpolating yaw with the physics alpha creates a
//       timebase mismatch on multi-tick or zero-tick frames, producing
//       visible jitter.
//
//   Movement keys (WASD / jump / crouch) — sampled once per physics tick
//       group when inputSyncedWithPhysics is true (the default) so
//       movement calculations match the server.  When the toggle is off,
//       keys are also sampled every iterate() call.
//
// Position interpolation uses alpha = accumulator / k_physicsDt across the
// LAST physics tick (PreviousPosition is saved inside the while loop before
// each tick).
//
// Three ImGui-tunable flags:
//
//   renderSeparateFromPhysics — render every iterate() call with position
//       interpolated between the last two physics ticks (true, default) vs.
//       render only after a physics tick (false, caps render fps at 128 Hz).
//
//   inputSyncedWithPhysics — sample movement keys once per tick group
//       (true, default, server-consistent) vs. every iterate() call (false).
//       Mouse look is always per-frame regardless of this toggle.
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

    static int iterCount = 0;
    if (false && ++iterCount <= 3)
        SDL_Log("[ITERATE] call=%d frameTime=%.4f acc=%.4f renderSep=%d",
                iterCount,
                static_cast<double>(frameTime),
                static_cast<double>(accumulator),
                renderSeparateFromPhysics);

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

    // ── 3. Input ───────────────────────────────────────────────────────────
    //
    // Mouse look runs EVERY iterate() call — this keeps camera rotation
    // perfectly smooth at whatever frame rate the renderer is producing.
    // SDL_GetRelativeMouseState returns accumulated delta since last call,
    // so total rotation is identical regardless of call frequency.
    //
    // Movement keys run once per physics tick group (when inputSyncedWithPhysics
    // is true) so WASD movement calculations match the server.  When the
    // sync toggle is off, movement keys also run every frame.
    if (mouseCaptured) {
        systems::runMouseLook(registry, mouseSensitivity);
        if (!inputSyncedWithPhysics)
            systems::runMovementKeys(registry);
    }

    // ── 4. Physics — always 128 Hz, up to k_maxTicksPerFrame catch-up ─────
    bool physicsRan = false;
    int ticksThisFrame = 0;

    if (accumulator >= k_physicsDt) {
        // Movement keys: sample once for this whole group of ticks.
        if (inputSyncedWithPhysics && mouseCaptured)
            systems::runMovementKeys(registry);

        while (accumulator >= k_physicsDt && ticksThisFrame < k_maxTicksPerFrame) {
            accumulator -= k_physicsDt;

            // Snapshot position before each tick so the last tick's delta is
            // available for interpolation (prevPos → pos over alpha ∈ [0,1]).
            registry.view<Position, PreviousPosition>().each(
                [](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });

            systems::runMovement(registry, k_physicsDt, physics::testWorld());
            systems::runCollision(registry, k_physicsDt, physics::testWorld());
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

                // Yaw is always used directly — no interpolation.
                //
                // When inputSyncedWithPhysics, yaw updates once per frame-group
                // (whenever the physics gate fires).  Interpolating yaw with the
                // *position* alpha is incorrect: position alpha spans one physics
                // tick, but yaw spans one frame of mouse input.  On multi-tick
                // or zero-tick frames the two timebases diverge, causing objects
                // to visually jitter.  Using yaw directly gives a consistent
                // per-frame rotation rate at the input sample rate (≥128 Hz with
                // VSync, or frame rate without), which is already smooth.
                renderYaw = input.yaw;
                renderPitch = input.pitch;
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

    // ── Flush dispatcher events (weapon fired, impact, explosion) ─────────
    dispatcher.update();

    // ── Update particle system (render-rate, not physics-rate) ────────────
    particleSystem.update(frameTime, renderer.getCamera(), registry);

    // Draw persistent HUD text each frame
    particleSystem.drawScreenText({10.f, 10.f}, "HP 100", {0.9f, 1.f, 0.9f, 1.f}, 22.f);

    // Compute camera forward and cache for event() key shortcuts
    {
        const float cosPitch = std::cos(renderPitch);
        cachedCamFwd_ =
            glm::vec3{std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
        cachedEye_ = renderEye;
    }

    // ── Update skeletal animation (CPU skinning) ─────────────────────────
    if (runAnimation.isLoaded() && animatedModelIdx >= 0) {
        runAnimation.update(frameTime);
        for (size_t m = 0; m < runAnimation.meshCount(); ++m) {
            const auto& sv = runAnimation.getSkinnedVertices(m);
            renderer.updateModelMeshVertices(
                animatedModelIdx, static_cast<int>(m), sv.data(), static_cast<Uint32>(sv.size()));
        }
    }

    // ── Build entity render list ────────────────────────────────────────────
    {
        std::vector<EntityRenderCmd> entityCmds;
        registry.view<Position, Renderable>().each([&](entt::entity e, const Position& pos, const Renderable& rend) {
            if (!rend.visible || rend.modelIndex < 0)
                return;
            // Skip local player model in first-person (Option A from the plan).
            if (registry.all_of<LocalPlayer>(e))
                return;

            glm::mat4 world = glm::translate(glm::mat4(1.0f), pos.value);
            world *= glm::mat4_cast(rend.orientation);
            world = glm::scale(world, rend.scale);

            entityCmds.push_back(EntityRenderCmd{.modelIndex = rend.modelIndex, .worldTransform = world});
        });
        renderer.setEntityRenderList(std::move(entityCmds));
    }

    // ── Build weapon viewmodel ──────────────────────────────────────────────
    // R-301 model bounds: X ±0.6, Y −3.4..+1.3, Z −8.4..+7.2
    // Model's +Z axis is the barrel direction.  Origin is near the grip.
    {
        WeaponViewmodel vm;
        if (weaponModelIdx >= 0) {
            vm.modelIndex = weaponModelIdx;
            vm.visible = true;

            const float cosPitch = std::cos(renderPitch);
            const glm::vec3 forward{
                std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
            const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0, 1, 0}));
            const glm::vec3 up = glm::normalize(glm::cross(right, forward));

            // Weapon bob: subtle sinusoidal offset based on movement speed
            float bobPhase = 0.0f;
            float bobAmplitude = 0.0f;
            registry.view<LocalPlayer, Velocity>().each([&](const Velocity& vel) {
                const float hSpeed = std::sqrt(vel.value.x * vel.value.x + vel.value.z * vel.value.z);
                if (hSpeed > 10.0f) {
                    bobAmplitude = std::min(hSpeed / 800.0f, 1.5f);
                    bobPhase = static_cast<float>(SDL_GetTicks()) * 0.008f;
                }
            });

            const float bobX = std::sin(bobPhase) * bobAmplitude;
            const float bobY = std::sin(bobPhase * 2.0f) * bobAmplitude * 0.5f;

            // FPS weapon position: lower-right of screen, slightly forward.
            // The model at scale 5 is ~75 units long, realistic for a rifle
            // in Quake-unit scale (1 unit ≈ 1 inch).
            constexpr float k_weaponScale = 5.0f;
            glm::vec3 weaponPos = renderEye + forward * 25.f + right * 18.f - up * 18.f;
            weaponPos += right * bobX + up * bobY;

            // Build world transform: translate → rotate → scale.
            // Rotation: model +Z (barrel) → camera forward,
            //           model +Y (rail)   → camera up,
            //           model +X (side)   → camera right.
            glm::mat4 weaponWorld = glm::translate(glm::mat4(1.0f), weaponPos);
            const glm::mat4 rotMat = glm::mat4(glm::vec4(right, 0.0f),   // column 0: model X → right
                                               glm::vec4(up, 0.0f),      // column 1: model Y → up
                                               glm::vec4(forward, 0.0f), // column 2: model Z → forward (barrel)
                                               glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            weaponWorld *= rotMat;
            weaponWorld = glm::scale(weaponWorld, glm::vec3(k_weaponScale));

            vm.transform = weaponWorld;
        }
        renderer.setWeaponViewmodel(vm);
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
                    renderer.ssrMode,
                    measuredPhysicsHz,
                    statsFPSCurrent,
                    statsFPSMin,
                    statsFPSMax,
                    statsFPS1pLow,
                    statsFPS5pLow);
    debugUI.buildParticleUI(particleSystem, cachedEye_, cachedCamFwd_);
    debugUI.buildRenderTogglesUI(renderer.toggles);
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
    particleSystem.quit();
    renderer.quit();
    debugUI.shutdown();
    client.shutdown();
    SDL_DestroyWindow(window);
    NET_Quit();
    SDL_Quit();
}
