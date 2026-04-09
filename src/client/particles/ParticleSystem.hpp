#pragma once

#include "ParticleEvents.hpp"
#include "ParticleRenderer.hpp"
#include "ecs/components/Projectile.hpp"
#include "ecs/registry/Registry.hpp"
#include "effects/BulletHoleDecal.hpp"
#include "effects/ExplosionEffect.hpp"
#include "effects/HitscanEffect.hpp"
#include "effects/ImpactEffect.hpp"
#include "effects/RibbonTrail.hpp"
#include "effects/SmokeEffect.hpp"
#include "effects/TracerEffect.hpp"
#include "renderer/Camera.hpp"
#include "sdf/SdfRenderer.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string_view>

/// @brief Top-level particle system orchestrator.
///
/// Owns all effect sub-systems and the ParticleRenderer.
/// Lifecycle:
///   Game::init()    → particleSystem.init()
///   Game::iterate() → particleSystem.update(dt, cam)
///   Renderer::drawFrame() calls internally:
///     particleSystem.uploadToGpu(cmd)  [before render pass]
///     particleSystem.render(pass, cmd) [inside render pass]
///   Game::quit()    → particleSystem.quit()
class ParticleSystem
{
public:
    bool init(SDL_GPUDevice* dev, SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt);
    void quit();

    // ── Spawn API (call from weapon / game event logic) ────────────────────

    /// @brief Attach an oriented-capsule tracer to a fast-bullet projectile entity.
    void spawnProjectileTracer(entt::entity e, Registry& reg);

    /// @brief Attach a ribbon trail to a slow/arcing projectile entity (rocket).
    void spawnRibbonTrail(entt::entity e, Registry& reg);

    /// @brief Spawn an instant energy beam from origin to hitPos.
    void spawnHitscanBeam(glm::vec3 origin, glm::vec3 hitPos, WeaponType wt);

    /// @brief Spawn spark burst + impact flash + bullet hole decal.
    void spawnImpactEffect(glm::vec3 pos, glm::vec3 normal, SurfaceType surf, WeaponType wt);

    /// @brief Place a bullet-hole decal on a surface.
    void spawnBulletHole(glm::vec3 pos, glm::vec3 normal, WeaponType wt);

    /// @brief Spawn a smoke cloud at pos.
    void spawnSmoke(glm::vec3 pos, float radius);

    /// @brief Spawn rocket explosion at pos.
    void spawnExplosion(glm::vec3 pos, float blastRadius);

    // ── SDF text (queued per frame, flushed in render) ─────────────────────

    void drawWorldText(glm::vec3 worldPos, std::string_view text, glm::vec4 color, float worldHeight);
    void drawScreenText(glm::vec2 pixelPos, std::string_view text, glm::vec4 color, float pixelHeight);

    // ── Frame lifecycle ────────────────────────────────────────────────────

    /// @brief Simulate all effects. Called once per render frame (not per physics tick).
    void update(float dt, const Camera& cam, Registry& reg);

    /// @brief Upload all particle data to GPU. Must be called BEFORE render pass.
    void uploadToGpu(SDL_GPUCommandBuffer* cmd);

    /// @brief Issue all particle draw calls. Must be called INSIDE a render pass.
    void render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd);

    // ── entt::dispatcher event handlers ───────────────────────────────────
    void onWeaponFired(const WeaponFiredEvent& e);
    void onImpact(const ProjectileImpactEvent& e);
    void onExplosion(const ExplosionEvent& e);

    // ── Live-count accessors (for debug UI) ───────────────────────────────
    [[nodiscard]] uint32_t impactCount() const { return impact_.count(); }
    [[nodiscard]] uint32_t tracerCount() const { return tracers_.count(); }
    [[nodiscard]] uint32_t ribbonVertexCount() const { return ribbons_.count(); }
    [[nodiscard]] uint32_t hitscanBeamCount() const { return hitscan_.beamCount(); }
    [[nodiscard]] uint32_t arcVertexCount() const { return hitscan_.arcCount(); }
    [[nodiscard]] uint32_t smokeCount() const { return smoke_.count(); }
    [[nodiscard]] uint32_t decalCount() const { return decals_.count(); }
    [[nodiscard]] bool sdfReady() const { return sdf_.ready(); }

private:
    ParticleRenderer renderer_;
    TracerEffect tracers_;
    RibbonTrail ribbons_;
    HitscanEffect hitscan_;
    SmokeEffect smoke_;
    ImpactEffect impact_;
    BulletHoleDecal decals_;
    ExplosionEffect explosions_;
    SdfRenderer sdf_;

    // Cached each frame from Camera
    glm::vec3 camPos_{};
    glm::vec3 camForward_{};
    glm::vec3 camRight_{};
    glm::vec3 camUp_{};
    float screenW_ = 1280.f;
    float screenH_ = 720.f;

    float frameDt_ = 0.016f; // last dt, needed by spawn callbacks
};
