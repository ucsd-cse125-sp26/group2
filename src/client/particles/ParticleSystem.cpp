/// @file ParticleSystem.cpp
/// @brief Implementation of the top-level particle system orchestrator.

#include "ParticleSystem.hpp"

#include "ecs/components/BeamState.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/RibbonEmitter.hpp"
#include "ecs/components/TracerEmitter.hpp"
#include "ecs/components/WeaponState.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

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
    update(dt, cam.getEye(), cam.getForward(), cam.getRight(), cam.getUp(), reg);
}

void ParticleSystem::update(float dt, glm::vec3 eye, glm::vec3 forward, glm::vec3 right, glm::vec3 up, Registry& reg)
{
    frameDt_ = dt;
    camPos_ = eye;
    camForward_ = glm::normalize(forward);
    camRight_ = glm::normalize(right);
    camUp_ = glm::normalize(up);

    tracers_.update(dt, reg);
    ribbons_.update(dt, reg, camPos_);
    hitscan_.update(dt, camForward_);

    // EnergyGun: drive the Railgun-derived sustained Tesla arc. The local
    // player uses the first-person muzzle marker when the viewmodel provides
    // one, otherwise falls back to the old hip-fire offset.
    reg.view<BeamState>().each([&](entt::entity e, const BeamState& beam) {
        if (!beam.active || beam.type != WeaponType::EnergyGun)
            return;
        glm::vec3 origin = beam.origin;
        glm::vec3 guidePoint = beam.guidePoint;
        glm::vec3 hitPoint = beam.hitPoint;
        if (reg.all_of<LocalPlayer>(e)) {
            float guideLen = glm::length(beam.guidePoint - beam.origin);
            if (guideLen < 0.5f)
                guideLen = glm::length(beam.hitPoint - beam.origin);
            if (guideLen < 0.5f)
                guideLen = 140.0f;
            origin = localEnergyBeamOriginOverrideValid_
                         ? localEnergyBeamOriginOverride_
                         : camPos_ + camForward_ * 6.0f + camRight_ * 5.0f - camUp_ * 5.0f;
            guidePoint = origin + camForward_ * guideLen;
            if (beam.locked == 0)
                hitPoint = guidePoint;
        }
        if (glm::length(guidePoint - origin) < 0.5f)
            guidePoint = hitPoint;
        energyTesla_.drive(
            static_cast<uint32_t>(e), origin, guidePoint, hitPoint, beam.locked != 0, beam.lockStrength);
    });
    localEnergyBeamOriginOverrideValid_ = false;
    tesla_.update(dt, camForward_);
    energyTesla_.update(dt, camForward_);

    smoke_.update(dt, reg, camPos_, camForward_);
    explosionVfx_.update(dt, reg, camPos_, camForward_);
    impact_.update(dt);
    decals_.update(dt);
    death_.update(dt);

    // Clear SDF queues for this frame (re-filled by drawWorldText/drawScreenText calls)
    sdf_.clear();
}

// uploadToGpu (copy pass, before render pass)

void ParticleSystem::uploadToGpu(SDL_GPUCommandBuffer* cmd)
{
    renderer_.uploadBillboards(cmd, impact_.data(), impact_.count());
    renderer_.uploadDissolve(cmd, death_.data(), death_.count());
    renderer_.uploadTracers(cmd, tracers_.data(), tracers_.count());
    renderer_.uploadRibbon(cmd, ribbons_.data(), ribbons_.count());
    renderer_.uploadHitscan(cmd, hitscan_.beamData(), hitscan_.beamCount());

    // Railgun and EnergyGun lightning share one arc vertex buffer / draw call.
    // Merge in priority order: Railgun, legacy arcs, EnergyGun main channels,
    // then EnergyGun forks/coronas. Truncation drops lower-priority details.
    constexpr uint32_t kMaxArcVerts = 8192;
    arcScratch_.clear();
    auto appendArcStream = [&](const ArcVertex* data, uint32_t count) {
        if (data == nullptr || count == 0 || arcScratch_.size() >= kMaxArcVerts)
            return;
        if (!arcScratch_.empty()) {
            arcScratch_.push_back(arcScratch_.back()); // degenerate strip restart
            arcScratch_.push_back(data[0]);
        }
        const size_t remaining = kMaxArcVerts - arcScratch_.size();
        const size_t n = std::min<size_t>(remaining, count);
        arcScratch_.insert(arcScratch_.end(), data, data + n);
    };
    appendArcStream(hitscan_.arcData(), hitscan_.arcCount());
    appendArcStream(tesla_.arcData(), tesla_.arcCount());
    appendArcStream(energyTesla_.mainArcData(), energyTesla_.mainArcCount());
    appendArcStream(energyTesla_.detailArcData(), energyTesla_.detailArcCount());
    if (arcScratch_.size() > kMaxArcVerts)
        arcScratch_.resize(kMaxArcVerts);
    renderer_.uploadArcs(cmd, arcScratch_.data(), static_cast<uint32_t>(arcScratch_.size()));
    renderer_.uploadSmoke(cmd, smoke_.data(), smoke_.count());
    renderer_.uploadDecals(cmd, decals_.data(), decals_.count());
    renderer_.uploadExplosionSprites(cmd, explosionVfx_.spriteData(), explosionVfx_.spriteCount());
    renderer_.uploadExplosionDebris(cmd, explosionVfx_.debrisData(), explosionVfx_.debrisCount());
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

void ParticleSystem::debugEnergyTeslaArc(glm::vec3 origin,
                                         glm::vec3 guidePoint,
                                         glm::vec3 hitPoint,
                                         bool locked,
                                         float lockStrength)
{
    energyTesla_.debugPulse(origin, guidePoint, hitPoint, locked, lockStrength);
}

void ParticleSystem::debugEnergyTeslaPreview(glm::vec3 origin,
                                             glm::vec3 guidePoint,
                                             glm::vec3 hitPoint,
                                             bool locked,
                                             float lockStrength)
{
    energyTesla_.debugPreview(origin, guidePoint, hitPoint, locked, lockStrength);
}

void ParticleSystem::spawnDeathDissolve(const std::vector<glm::vec3>& worldPoints, glm::vec3 center, glm::vec4 color)
{
    death_.spawn(worldPoints, center, color);
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
