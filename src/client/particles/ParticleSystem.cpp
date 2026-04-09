#include "ParticleSystem.hpp"

#include "ecs/components/RibbonEmitter.hpp"
#include "ecs/components/TracerEmitter.hpp"

#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// init / quit
// ---------------------------------------------------------------------------

bool ParticleSystem::init(SDL_GPUDevice* dev, SDL_GPUTextureFormat colorFmt, SDL_GPUShaderFormat shaderFmt)
{
    if (!renderer_.init(dev, colorFmt, shaderFmt)) {
        SDL_Log("ParticleSystem: ParticleRenderer init failed");
        return false;
    }

    // SDF font — try a common system font path; silently skip if not found
    const char* fontPaths[] = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                               "/usr/share/fonts/TTF/DejaVuSans.ttf",
                               "/System/Library/Fonts/Helvetica.ttc",
                               nullptr};
    for (int i = 0; fontPaths[i]; ++i) {
        if (sdf_.init(dev, fontPaths[i]))
            break;
    }
    if (!sdf_.ready())
        SDL_Log("ParticleSystem: SDF font not loaded — text rendering disabled");

    return true;
}

void ParticleSystem::quit()
{
    sdf_.quit();
    renderer_.quit();
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

void ParticleSystem::update(float dt, const Camera& cam, Registry& reg)
{
    frameDt_ = dt;
    camPos_ = cam.getEye();
    camForward_ = cam.getForward();
    camRight_ = cam.getRight();
    camUp_ = cam.getUp();

    tracers_.update(dt, reg);
    ribbons_.update(dt, reg, camPos_);
    hitscan_.update(dt);
    smoke_.update(dt, reg, camPos_, camForward_);
    impact_.update(dt);
    decals_.update(dt);
    explosions_.update(dt);

    // Flush deferred explosion smoke
    for (int i = 0; i < explosions_.ringCount(); ++i) {
        // Handled inside explosions_.update — smoke spawns via pending list
    }

    // Clear SDF queues for this frame (re-filled by drawWorldText/drawScreenText calls)
    sdf_.clear();
}

// ---------------------------------------------------------------------------
// uploadToGpu (copy pass, before render pass)
// ---------------------------------------------------------------------------

void ParticleSystem::uploadToGpu(SDL_GPUCommandBuffer* cmd)
{
    renderer_.uploadBillboards(cmd, impact_.data(), impact_.count());
    renderer_.uploadTracers(cmd, tracers_.data(), tracers_.count());
    renderer_.uploadRibbon(cmd, ribbons_.data(), ribbons_.count());
    renderer_.uploadHitscan(cmd, hitscan_.beamData(), hitscan_.beamCount());
    renderer_.uploadArcs(cmd, hitscan_.arcData(), hitscan_.arcCount());
    renderer_.uploadSmoke(cmd, smoke_.data(), smoke_.count());
    renderer_.uploadDecals(cmd, decals_.data(), decals_.count());
    renderer_.uploadSdfWorld(cmd, sdf_.worldData(), sdf_.worldCount());
    renderer_.uploadSdfHud(cmd, sdf_.hudData(), sdf_.hudCount());
}

// ---------------------------------------------------------------------------
// render (inside render pass)
// ---------------------------------------------------------------------------

void ParticleSystem::render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd)
{
    renderer_.drawAll(pass, cmd, screenW_, screenH_);
}

// ---------------------------------------------------------------------------
// Spawn API
// ---------------------------------------------------------------------------

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

void ParticleSystem::spawnHitscanBeam(glm::vec3 origin, glm::vec3 hitPos, WeaponType wt)
{
    hitscan_.spawn(origin, hitPos, wt, camForward_);
}

void ParticleSystem::spawnImpactEffect(glm::vec3 pos, glm::vec3 normal, SurfaceType surf, WeaponType wt)
{
    impact_.spawn(pos, normal, surf, frameDt_);
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

void ParticleSystem::spawnExplosion(glm::vec3 pos, float blastRadius)
{
    explosions_.spawn(pos, blastRadius, smoke_);
}

// ---------------------------------------------------------------------------
// SDF text
// ---------------------------------------------------------------------------

void ParticleSystem::drawWorldText(glm::vec3 worldPos, std::string_view text, glm::vec4 color, float worldHeight)
{
    sdf_.drawWorldText(worldPos, text, color, worldHeight, camRight_, camUp_);
}

void ParticleSystem::drawScreenText(glm::vec2 pixelPos, std::string_view text, glm::vec4 color, float pixelHeight)
{
    sdf_.drawScreenText(pixelPos, text, color, pixelHeight);
}

// ---------------------------------------------------------------------------
// entt::dispatcher event handlers
// ---------------------------------------------------------------------------

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
    spawnExplosion(e.pos, e.blastRadius);
}
