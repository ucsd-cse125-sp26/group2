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
#include "particles/ParticleEvents.hpp"
#include "systems/InputSampleSystem.hpp"

#include <SDL3/SDL_video.h>

#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <glm/glm.hpp>

// World geometry for the current test scene: a single floor plane at y=0.
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

    // Particle system needs the device + formats from the renderer.
    if (!particleSystem.init(renderer.getDevice(), renderer.colorFormat, renderer.shaderFormat)) {
        SDL_Log("ParticleSystem init failed (non-fatal — particles disabled)");
        // Non-fatal: game can run without particles
    } else {
        renderer.setParticleSystem(&particleSystem);

        // Wire dispatcher events to particle system
        dispatcher.sink<WeaponFiredEvent>().connect<&ParticleSystem::onWeaponFired>(particleSystem);
        dispatcher.sink<ProjectileImpactEvent>().connect<&ParticleSystem::onImpact>(particleSystem);
        dispatcher.sink<ExplosionEvent>().connect<&ParticleSystem::onExplosion>(particleSystem);
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
    debugUI.processEvent(event);

    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    if (event->type == SDL_EVENT_KEY_DOWN) {
        switch (event->key.key) {
        case SDLK_Q:
            return SDL_APP_SUCCESS;

        case SDLK_ESCAPE:
            mouseCaptured = !mouseCaptured;
            SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
            break;

        case SDLK_F1: {
            static constexpr char k_helloMsg[] = "Hello from client!";
            client.send(k_helloMsg, static_cast<int>(sizeof(k_helloMsg) - 1));
            SDL_Log("Sent test packet to server");
            break;
        }

        // ── Particle system test keys ───────────────────────────────────────
        // cachedEye/camFwd are updated every iterate() frame; at worst one frame stale.
        case SDLK_T: {
            // Hitscan energy beam from hip-fire position
            const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
            const glm::vec3 hip = cachedEye_ + right * 15.f - glm::vec3{0, 1, 0} * 8.f + cachedCamFwd_ * 5.f;
            particleSystem.spawnHitscanBeam(hip, hip + cachedCamFwd_ * 400.f, WeaponType::EnergyRifle);
            particleSystem.spawnImpactEffect(
                hip + cachedCamFwd_ * 400.f, -cachedCamFwd_, SurfaceType::Energy, WeaponType::EnergyRifle);
            break;
        }
        case SDLK_Y: {
            // Bullet tracer from hip-fire position
            const glm::vec3 right = glm::normalize(glm::cross(cachedCamFwd_, glm::vec3{0, 1, 0}));
            const glm::vec3 hip = cachedEye_ + right * 15.f - glm::vec3{0, 1, 0} * 8.f + cachedCamFwd_ * 5.f;
            particleSystem.spawnBulletTracer(hip, cachedCamFwd_, 400.f);
            particleSystem.spawnImpactEffect(
                hip + cachedCamFwd_ * 400.f, -cachedCamFwd_, SurfaceType::Metal, WeaponType::Rifle);
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
    if (mouseCaptured)
        systems::runInputSample(registry);

    // Compute frame time.
    const Uint64 k_perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 k_now = SDL_GetPerformanceCounter();

    float frameTime = static_cast<float>(k_now - prevTime) / static_cast<float>(k_perfFreq);
    prevTime = k_now;
    frameTime = std::min(frameTime, 0.25f);
    accumulator += frameTime;

    // Fixed-step physics loop.
    while (accumulator >= k_physicsDt) {
        registry.view<Position, PreviousPosition>().each(
            [](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });

        systems::runMovement(registry, k_physicsDt);
        systems::runCollision(registry, k_physicsDt, k_worldPlanes);

        accumulator -= k_physicsDt;
        ++tickCount;
    }

    // Flush dispatcher events (weapon fired, impact, explosion).
    dispatcher.update();

    // Network receive.
    while (client.poll()) {
    }

    // ── Resolve first-person camera from local player ──────────────────────
    glm::vec3 renderEye{0.0f, 100.0f, 0.0f};
    float renderYaw = 0.0f;
    float renderPitch = 0.0f;

    registry.view<LocalPlayer, Position, PreviousPosition, InputSnapshot, CollisionShape>().each(
        [&](const Position& pos,
            const PreviousPosition& prev,
            const InputSnapshot& input,
            const CollisionShape& shape) {
            const float alpha = accumulator / k_physicsDt;
            const glm::vec3 interp = glm::mix(prev.value, pos.value, alpha);
            const float eyeOffset = shape.halfExtents.y * 0.77f;
            renderEye = interp + glm::vec3{0.0f, eyeOffset, 0.0f};
            renderYaw = input.yaw;
            renderPitch = input.pitch;
        });

    // ── Update particle system (render-rate, not physics-rate) ────────────
    particleSystem.update(frameTime, renderer.getCamera(), registry);

    // Draw persistent HUD text each frame
    particleSystem.drawScreenText({10.f, 10.f}, "HP 100", {0.9f, 1.f, 0.9f, 1.f}, 22.f);

    // Compute camera forward and cache for event() key shortcuts
    const float cosPitch = std::cos(renderPitch);
    const glm::vec3 camFwd{std::sin(renderYaw) * cosPitch, -std::sin(renderPitch), std::cos(renderYaw) * cosPitch};
    cachedEye_ = renderEye;
    cachedCamFwd_ = camFwd;

    // Build debug UI and render.
    debugUI.buildUI(registry, tickCount);
    debugUI.buildParticleUI(particleSystem, renderEye, camFwd);
    debugUI.render();
    renderer.drawFrame(renderEye, renderYaw, renderPitch);

    return SDL_APP_CONTINUE;
}

void Game::quit()
{
    particleSystem.quit();
    renderer.quit();
    debugUI.shutdown();
    client.shutdown();
    SDL_DestroyWindow(window);
    NET_Quit();
    SDL_Quit();
}
