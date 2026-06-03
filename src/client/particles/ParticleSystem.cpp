/// @file ParticleSystem.cpp
/// @brief Implementation of the top-level particle system orchestrator.

#include "ParticleSystem.hpp"

#include "ecs/components/BeamState.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/RibbonEmitter.hpp"
#include "ecs/components/TracerEmitter.hpp"
#include "ecs/components/WeaponState.hpp"

#include <SDL3/SDL.h>

// init / quit

bool ParticleSystem::init(SDL_GPUDevice* dev, SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt)
{
    if (!renderer_.init(dev, colorFmt, shaderFmt)) {
        SDL_Log("ParticleSystem: ParticleRenderer init failed");
        return false;
    }

    const char* base = SDL_GetBasePath();
    const std::string customFont = std::string(base ? base : "") + "assets/fonts/SpaceGrotesk.ttf";

    const char* fontPaths[] = {customFont.c_str(),
                               "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
                               "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                               "/usr/share/fonts/TTF/DejaVuSans.ttf",
                               "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                               "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
                               "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                               "/System/Library/Fonts/Supplemental/Arial.ttf",
                               "/Library/Fonts/Arial.ttf",
                               "/System/Library/Fonts/SFNS.ttf",
                               "C:/Windows/Fonts/segoeui.ttf",
                               nullptr};
    for (int i = 0; fontPaths[i]; ++i) {
        if (sdf_.init(dev, fontPaths[i]))
            break;
    }
    if (!sdf_.ready()) {
        SDL_Log("ParticleSystem: SDF font not loaded — text rendering disabled");
    } else {
        // Register atlas texture+sampler with the renderer so drawAll() can bind it.
        renderer_.setSdfAtlas(sdf_.atlas().gpuTexture(), sdf_.atlas().gpuSampler());
    }

    return true;
}

void ParticleSystem::quit()
{
    sdf_.quit();
    renderer_.quit();
}

// update

void ParticleSystem::update(float dt, const NewCamera& cam, Registry& reg)
{
    frameDt_ = dt;
    camPos_ = cam.getEye();
    camForward_ = cam.getForward();
    camRight_ = cam.getRight();
    camUp_ = cam.getUp();

    tracers_.update(dt, reg);
    ribbons_.update(dt, reg, camPos_);
    hitscan_.update(dt, camForward_);

    // Tesla Cannon: drive a sustained, curved arc from each active EnergyGun
    // beam to its locked target (server-set BeamState.hitPoint). The local
    // player's arc is offset to a hip-fire muzzle so it doesn't shoot from the
    // camera's centre.
    reg.view<BeamState>().each([&](entt::entity e, const BeamState& beam) {
        if (!beam.active || beam.type != WeaponType::EnergyGun)
            return;
        glm::vec3 origin = beam.origin;
        if (reg.all_of<LocalPlayer>(e))
            origin = camPos_ + camForward_ * 6.0f + camRight_ * 5.0f - camUp_ * 5.0f;
        tesla_.drive(static_cast<uint32_t>(e), origin, beam.hitPoint);
    });
    tesla_.update(dt, camForward_);

    smoke_.update(dt, reg, camPos_, camForward_);
    explosionVfx_.update(dt, reg, camPos_, camForward_);
    impact_.update(dt);
    decals_.update(dt);

    // Clear SDF queues for this frame (re-filled by drawWorldText/drawScreenText calls)
    sdf_.clear();
}

// uploadToGpu (copy pass, before render pass)

void ParticleSystem::uploadToGpu(SDL_GPUCommandBuffer* cmd)
{
    renderer_.uploadBillboards(cmd, impact_.data(), impact_.count());
    renderer_.uploadTracers(cmd, tracers_.data(), tracers_.count());
    renderer_.uploadRibbon(cmd, ribbons_.data(), ribbons_.count());
    renderer_.uploadHitscan(cmd, hitscan_.beamData(), hitscan_.beamCount());

    // Both lightning effects share one arc vertex buffer / draw call. Merge
    // them, inserting a degenerate join so the two triangle strips don't link.
    // Clamp to the GPU buffer capacity so an overflow can never read OOB.
    constexpr uint32_t kMaxArcVerts = 4096;
    arcScratch_.clear();
    const ArcVertex* h = hitscan_.arcData();
    const uint32_t hN = hitscan_.arcCount();
    arcScratch_.insert(arcScratch_.end(), h, h + hN);
    const ArcVertex* t = tesla_.arcData();
    const uint32_t tN = tesla_.arcCount();
    if (tN > 0) {
        if (!arcScratch_.empty()) {
            arcScratch_.push_back(arcScratch_.back()); // degenerate strip restart
            arcScratch_.push_back(t[0]);
        }
        arcScratch_.insert(arcScratch_.end(), t, t + tN);
    }
    if (arcScratch_.size() > kMaxArcVerts)
        arcScratch_.resize(kMaxArcVerts);
    renderer_.uploadArcs(cmd, arcScratch_.data(), static_cast<uint32_t>(arcScratch_.size()));
    renderer_.uploadSmoke(cmd, smoke_.data(), smoke_.count());
    renderer_.uploadDecals(cmd, decals_.data(), decals_.count());
    renderer_.uploadExplosionSprites(cmd, explosionVfx_.spriteData(), explosionVfx_.spriteCount());
    renderer_.uploadExplosionDebris(cmd, explosionVfx_.debrisData(), explosionVfx_.debrisCount());
    renderer_.uploadExplosionDecals(cmd, explosionVfx_.decalData(), explosionVfx_.decalCount());
    renderer_.uploadSdfWorld(cmd, sdf_.worldData(), sdf_.worldCount());
    renderer_.uploadSdfHud(cmd, sdf_.hudData(), sdf_.hudCount());
}

// render (inside render pass)

void ParticleSystem::render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd)
{
    renderer_.drawAll(pass, cmd, screenW_, screenH_);
}

// Spawn API

void ParticleSystem::spawnProjectileTracer(entt::entity e, Registry& reg)
{
    // Attach TracerEmitter component if not already present
    if (!reg.all_of<TracerEmitter>(e))
        reg.emplace<TracerEmitter>(e);
    tracers_.attach(e, reg);
}

void ParticleSystem::spawnRibbonTrail(entt::entity e, Registry& reg)
{
    if (!reg.all_of<RibbonEmitter>(e))
        reg.emplace<RibbonEmitter>(e);
}

void ParticleSystem::spawnBulletTracer(glm::vec3 origin, glm::vec3 dir, float range)
{
    tracers_.spawnRifleTracer(origin, dir, range);
}

void ParticleSystem::spawnHitscanBeam(glm::vec3 origin, glm::vec3 hitPos, WeaponType wt)
{
    hitscan_.spawn(origin, hitPos, wt, camForward_);
}

void ParticleSystem::spawnImpactEffect(glm::vec3 pos, glm::vec3 normal, SurfaceType surf, WeaponType wt)
{
    impact_.spawn(pos, normal, surf, frameDt_);
    // Don't leave bullet-hole decals on players — only on world surfaces.
    if (surf != SurfaceType::Flesh)
        spawnBulletHole(pos, normal, wt);
}

void ParticleSystem::spawnBulletHole(glm::vec3 pos, glm::vec3 normal, WeaponType wt)
{
    decals_.spawn(pos, normal, wt);
}

void ParticleSystem::spawnSmoke(glm::vec3 pos, float radius)
{
    smoke_.spawn(pos, radius);
}

void ParticleSystem::spawnFire(glm::vec3 pos, float radius)
{
    smoke_.spawn(pos, radius, /*isFire=*/true);
}

void ParticleSystem::spawnExplosion(glm::vec3 pos, float blastRadius)
{
    spawnExplosionVfx(pos, glm::vec3{0.0f, 1.0f, 0.0f}, blastRadius, ExplosionVfxKind::Rocket);
}

void ParticleSystem::spawnExplosionVfx(glm::vec3 pos, glm::vec3 normal, float blastRadius, ExplosionVfxKind kind)
{
    explosionVfx_.spawn(pos, normal, blastRadius, kind);
}

void ParticleSystem::driveGroundFire(entt::entity fieldEntity, glm::vec3 pos, float radius, float remaining, float duration)
{
    explosionVfx_.driveGroundFire(fieldEntity, pos, radius, remaining, duration);
}

// SDF text

void ParticleSystem::drawWorldText(glm::vec3 worldPos, std::string_view text, glm::vec4 color, float worldHeight)
{
    sdf_.drawWorldText(worldPos, text, color, worldHeight, camRight_, camUp_);
}

void ParticleSystem::drawScreenText(glm::vec2 pixelPos, std::string_view text, glm::vec4 color, float pixelHeight)
{
    sdf_.drawScreenText(pixelPos, text, color, pixelHeight);
}

// entt::dispatcher event handlers

void ParticleSystem::onWeaponFired(const WeaponFiredEvent& e)
{
    if (e.isHitscan)
        spawnHitscanBeam(e.origin, e.hitPos, e.type);
}

void ParticleSystem::onImpact(const ProjectileImpactEvent& e)
{
    spawnImpactEffect(e.pos, e.normal, e.surface, e.weaponType);
}

void ParticleSystem::onExplosion(const ExplosionEvent& e)
{
    spawnExplosionVfx(e.pos, e.normal, e.blastRadius, explosionVfxKindForWeapon(e.weaponType));
}
