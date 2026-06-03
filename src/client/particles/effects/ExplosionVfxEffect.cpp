/// @file ExplosionVfxEffect.cpp
/// @brief Fresh layered explosion, fire, smoke, spark, and scorch VFX.

#include "ExplosionVfxEffect.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace
{

float randf()
{
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}

float randRange(float lo, float hi)
{
    return lo + randf() * (hi - lo);
}

glm::vec3 randomUnit()
{
    const float z = randRange(-1.0f, 1.0f);
    const float a = randf() * glm::two_pi<float>();
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return {std::cos(a) * r, z, std::sin(a) * r};
}

glm::vec3 randomHemisphere(glm::vec3 normal, float upwardBias = 0.0f)
{
    glm::vec3 dir = randomUnit();
    if (glm::dot(dir, normal) < 0.0f)
        dir = -dir;
    dir.y += upwardBias;
    return glm::normalize(dir);
}

glm::vec3 tangentAround(glm::vec3 normal)
{
    const glm::vec3 up = std::abs(normal.y) > 0.9f ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    return glm::normalize(glm::cross(normal, up));
}

glm::vec3 ringDir(glm::vec3 normal)
{
    const glm::vec3 right = tangentAround(normal);
    const glm::vec3 fwd = glm::normalize(glm::cross(right, normal));
    const float a = randf() * glm::two_pi<float>();
    return glm::normalize(right * std::cos(a) + fwd * std::sin(a));
}

float safeDuration(float remaining, float duration)
{
    return std::max(duration, remaining + 0.001f);
}

} // namespace

ExplosionVfxKind explosionVfxKindForWeapon(WeaponType type)
{
    switch (type) {
    case WeaponType::HEGrenade:
        return ExplosionVfxKind::Frag;
    case WeaponType::Sticky:
        return ExplosionVfxKind::Sticky;
    case WeaponType::Molotov:
        return ExplosionVfxKind::Molotov;
    case WeaponType::Rocket:
    default:
        return ExplosionVfxKind::Rocket;
    }
}

void ExplosionVfxEffect::spawnSprite(glm::vec3 pos,
                                     glm::vec3 vel,
                                     float size,
                                     glm::vec4 color,
                                     float lifetime,
                                     float growthRate,
                                     float spinRate,
                                     float rotation,
                                     int frameStart,
                                     int frameCount,
                                     float fps,
                                     int material,
                                     float stretch,
                                     float softness,
                                     float priority)
{
    auto* p = spritePool_.spawn();
    if (!p)
        return;
    p->pos = pos;
    p->size = size;
    p->vel = vel;
    p->rotation = rotation;
    p->color = color;
    p->age = {0.0f, std::max(0.01f, lifetime), growthRate, spinRate};
    p->anim = {static_cast<float>(frameStart), static_cast<float>(frameCount), fps, static_cast<float>(material)};
    p->shape = {stretch, static_cast<float>(nextSeed() & 0xffffu) / 65535.0f, softness, priority};
}

void ExplosionVfxEffect::spawnDebris(glm::vec3 pos,
                                     glm::vec3 vel,
                                     float size,
                                     glm::vec4 color,
                                     float lifetime,
                                     float gravity,
                                     float drag,
                                     float stretch)
{
    auto* p = debrisPool_.spawn();
    if (!p)
        return;
    p->pos = pos;
    p->size = size;
    p->vel = vel;
    p->stretch = stretch;
    p->color = color;
    p->sim = {0.0f, std::max(0.01f, lifetime), gravity, drag};
}

void ExplosionVfxEffect::spawnScorch(glm::vec3 pos, glm::vec3 normal, float size, float opacity, ExplosionVfxKind kind)
{
    const uint32_t idx = decalHead_ % k_maxDecals;
    ++decalHead_;

    DecalInstance& d = decals_[idx];
    d.pos = pos + normal * 0.8f;
    d.size = size;
    d.opacity = opacity;
    d.right = tangentAround(normal);
    d.up = glm::normalize(glm::cross(d.right, normal));
    d._p0 = 0.0f;

    // The procedural atlas reserves rows for warmer soot, dust, and blue plasma marks.
    switch (kind) {
    case ExplosionVfxKind::Sticky:
        d.uvMin = {0.50f, 0.75f};
        d.uvMax = {0.75f, 1.00f};
        break;
    case ExplosionVfxKind::Frag:
        d.uvMin = {0.25f, 0.75f};
        d.uvMax = {0.50f, 1.00f};
        break;
    case ExplosionVfxKind::Molotov:
        d.uvMin = {0.75f, 0.75f};
        d.uvMax = {1.00f, 1.00f};
        break;
    case ExplosionVfxKind::Rocket:
    default:
        d.uvMin = {0.00f, 0.75f};
        d.uvMax = {0.25f, 1.00f};
        break;
    }
}

void ExplosionVfxEffect::spawn(glm::vec3 pos, glm::vec3 normal, float radius, ExplosionVfxKind kind)
{
    if (glm::dot(normal, normal) < 1e-4f)
        normal = {0.0f, 1.0f, 0.0f};
    normal = glm::normalize(normal);
    radius = std::max(24.0f, radius);

    switch (kind) {
    case ExplosionVfxKind::Frag:
        spawnFrag(pos, normal, radius);
        break;
    case ExplosionVfxKind::Sticky:
        spawnSticky(pos, normal, radius);
        break;
    case ExplosionVfxKind::Molotov:
        spawnMolotovBurst(pos, normal, radius);
        break;
    case ExplosionVfxKind::Rocket:
    default:
        spawnRocket(pos, normal, radius);
        break;
    }
}

void ExplosionVfxEffect::spawnRocket(glm::vec3 pos, glm::vec3 normal, float radius)
{
    spawnSprite(pos, {}, radius * 0.42f, {1.0f, 0.78f, 0.38f, 0.95f}, 0.14f, 4.6f, 0.0f, randf(), 0, 4, 34.0f, 0, 1.0f, 0.50f, 1.0f);
    spawnSprite(pos + normal * 4.0f, {}, radius * 0.30f, {1.0f, 0.43f, 0.08f, 0.90f}, 0.38f, 2.1f, randRange(-2.5f, 2.5f), randf(), 4, 12, 30.0f, 1, 1.0f, 0.45f, 0.9f);
    spawnSprite(pos + normal * 2.0f, {}, radius * 0.20f, {1.0f, 0.48f, 0.08f, 0.75f}, 0.28f, 5.4f, 0.0f, randf(), 32, 4, 18.0f, 3, 1.0f, 0.25f, 0.8f);

    for (int i = 0; i < 9; ++i) {
        const glm::vec3 dir = randomHemisphere(normal, 0.35f);
        spawnSprite(pos + dir * randRange(6.0f, radius * 0.12f),
                    dir * randRange(45.0f, 135.0f),
                    randRange(radius * 0.13f, radius * 0.22f),
                    {0.09f, 0.085f, 0.075f, randRange(0.30f, 0.45f)},
                    randRange(1.3f, 2.2f),
                    randRange(0.45f, 0.85f),
                    randRange(-0.8f, 0.8f),
                    randf(),
                    16,
                    8,
                    10.0f,
                    2,
                    1.0f,
                    0.75f,
                    0.35f);
    }
    for (int i = 0; i < 28; ++i) {
        glm::vec3 dir = randomHemisphere(normal, 0.15f);
        spawnDebris(pos + dir * 5.0f,
                    dir * randRange(260.0f, 760.0f),
                    randRange(1.5f, 3.0f),
                    {1.0f, randRange(0.48f, 0.72f), 0.12f, 1.0f},
                    randRange(0.28f, 0.72f),
                    randRange(300.0f, 780.0f),
                    1.8f,
                    randRange(3.0f, 7.0f));
    }
    spawnScorch(pos, normal, radius * 0.42f, 0.95f, ExplosionVfxKind::Rocket);
}

void ExplosionVfxEffect::spawnFrag(glm::vec3 pos, glm::vec3 normal, float radius)
{
    spawnSprite(pos, {}, radius * 0.24f, {1.0f, 0.86f, 0.55f, 0.88f}, 0.10f, 4.0f, 0.0f, randf(), 0, 4, 40.0f, 0, 1.0f, 0.45f, 1.0f);
    spawnSprite(pos + normal * 2.0f, {}, radius * 0.36f, {0.42f, 0.35f, 0.27f, 0.52f}, 0.85f, 1.1f, randRange(-1.0f, 1.0f), randf(), 24, 8, 12.0f, 2, 1.0f, 0.80f, 0.45f);
    spawnSprite(pos + normal * 1.5f, {}, radius * 0.18f, {0.68f, 0.52f, 0.32f, 0.60f}, 0.30f, 4.8f, 0.0f, randf(), 36, 4, 16.0f, 3, 1.0f, 0.30f, 0.7f);

    for (int i = 0; i < 12; ++i) {
        const glm::vec3 dir = ringDir(normal);
        spawnSprite(pos + dir * randRange(8.0f, 20.0f),
                    dir * randRange(85.0f, 190.0f) + normal * randRange(12.0f, 42.0f),
                    randRange(radius * 0.08f, radius * 0.13f),
                    {0.34f, 0.30f, 0.25f, randRange(0.26f, 0.40f)},
                    randRange(0.7f, 1.4f),
                    randRange(0.6f, 1.1f),
                    randRange(-0.7f, 0.7f),
                    randf(),
                    24,
                    8,
                    10.0f,
                    2,
                    1.0f,
                    0.85f,
                    0.35f);
    }
    for (int i = 0; i < 46; ++i) {
        glm::vec3 dir = randomHemisphere(normal, 0.08f);
        spawnDebris(pos + dir * 3.0f,
                    dir * randRange(340.0f, 900.0f),
                    randRange(1.1f, 2.4f),
                    {1.0f, 0.76f, 0.38f, 1.0f},
                    randRange(0.22f, 0.55f),
                    randRange(520.0f, 950.0f),
                    2.5f,
                    randRange(4.0f, 9.0f));
    }
    spawnScorch(pos, normal, radius * 0.34f, 0.82f, ExplosionVfxKind::Frag);
}

void ExplosionVfxEffect::spawnSticky(glm::vec3 pos, glm::vec3 normal, float radius)
{
    spawnSprite(pos, {}, radius * 0.28f, {0.58f, 0.88f, 1.0f, 0.95f}, 0.11f, 5.2f, 0.0f, randf(), 40, 4, 38.0f, 0, 1.0f, 0.35f, 1.0f);
    spawnSprite(pos + normal * 2.0f, {}, radius * 0.20f, {0.10f, 0.78f, 1.0f, 0.88f}, 0.20f, 6.0f, 0.0f, randf(), 44, 4, 30.0f, 3, 1.0f, 0.18f, 0.9f);
    spawnSprite(pos + normal * 2.0f, {}, radius * 0.30f, {0.04f, 0.44f, 1.0f, 0.50f}, 0.26f, 5.3f, 0.0f, randf(), 48, 4, 24.0f, 3, 1.0f, 0.20f, 0.8f);

    for (int i = 0; i < 24; ++i) {
        glm::vec3 dir = randomHemisphere(normal, 0.03f);
        spawnDebris(pos + dir * 4.0f,
                    dir * randRange(280.0f, 820.0f),
                    randRange(1.0f, 2.0f),
                    {0.25f, 0.90f, 1.0f, 1.0f},
                    randRange(0.18f, 0.42f),
                    randRange(80.0f, 180.0f),
                    0.7f,
                    randRange(5.0f, 10.0f));
    }
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 dir = randomHemisphere(normal, 0.4f);
        spawnSprite(pos + dir * 8.0f,
                    dir * randRange(18.0f, 55.0f),
                    randRange(radius * 0.08f, radius * 0.13f),
                    {0.12f, 0.28f, 0.35f, 0.24f},
                    randRange(0.55f, 0.9f),
                    randRange(0.8f, 1.2f),
                    randRange(-1.2f, 1.2f),
                    randf(),
                    16,
                    8,
                    12.0f,
                    2,
                    1.0f,
                    0.70f,
                    0.25f);
    }
    spawnScorch(pos, normal, radius * 0.22f, 0.70f, ExplosionVfxKind::Sticky);
}

void ExplosionVfxEffect::spawnMolotovBurst(glm::vec3 pos, glm::vec3 normal, float radius)
{
    spawnSprite(pos + normal * 2.0f, {}, radius * 0.20f, {1.0f, 0.42f, 0.05f, 0.85f}, 0.16f, 5.4f, 0.0f, randf(), 52, 4, 28.0f, 0, 1.0f, 0.35f, 0.9f);
    for (int i = 0; i < 14; ++i) {
        const glm::vec3 dir = ringDir(normal);
        spawnSprite(pos + dir * randRange(8.0f, 35.0f),
                    dir * randRange(70.0f, 160.0f) + normal * randRange(18.0f, 70.0f),
                    randRange(radius * 0.08f, radius * 0.14f),
                    {1.0f, randRange(0.28f, 0.50f), 0.04f, randRange(0.60f, 0.88f)},
                    randRange(0.35f, 0.7f),
                    randRange(1.1f, 1.9f),
                    randRange(-2.4f, 2.4f),
                    randf(),
                    56,
                    8,
                    18.0f,
                    4,
                    randRange(1.0f, 1.8f),
                    0.35f,
                    0.7f);
    }
    for (int i = 0; i < 16; ++i) {
        glm::vec3 dir = randomHemisphere(normal, 0.65f);
        spawnDebris(pos,
                    dir * randRange(90.0f, 320.0f),
                    randRange(1.0f, 2.2f),
                    {1.0f, 0.40f, 0.05f, 1.0f},
                    randRange(0.55f, 1.2f),
                    randRange(-120.0f, 80.0f),
                    0.8f,
                    randRange(2.0f, 5.0f));
    }
    spawnScorch(pos, normal, radius * 0.50f, 0.72f, ExplosionVfxKind::Molotov);
}

void ExplosionVfxEffect::driveGroundFire(entt::entity fieldEntity,
                                         glm::vec3 pos,
                                         float radius,
                                         float remaining,
                                         float duration)
{
    auto it = std::find_if(groundFire_.begin(), groundFire_.end(), [&](const GroundFireAnchor& a) {
        return a.entity == fieldEntity;
    });
    if (it == groundFire_.end()) {
        GroundFireAnchor anchor;
        anchor.entity = fieldEntity;
        anchor.seed = nextSeed();
        it = groundFire_.insert(groundFire_.end(), anchor);
    }
    it->pos = pos;
    it->radius = std::max(16.0f, radius);
    it->remaining = std::max(0.0f, remaining);
    it->duration = safeDuration(remaining, duration);
}

void ExplosionVfxEffect::tickGroundFire(GroundFireAnchor& anchor, float dt)
{
    if (!anchor.spawnedBurst) {
        spawnMolotovBurst(anchor.pos, {0.0f, 1.0f, 0.0f}, anchor.radius);
        anchor.spawnedBurst = true;
    }

    const float lifeFrac = anchor.duration > 0.0f ? glm::clamp(anchor.remaining / anchor.duration, 0.0f, 1.0f) : 0.0f;
    const float edgeFade = glm::clamp(lifeFrac * 4.0f, 0.0f, 1.0f);
    anchor.flameAccumulator += dt * 34.0f * edgeFade;
    anchor.smokeAccumulator += dt * 6.0f * edgeFade;
    anchor.emberAccumulator += dt * 18.0f * edgeFade;

    while (anchor.flameAccumulator >= 1.0f) {
        anchor.flameAccumulator -= 1.0f;
        const glm::vec3 dir = ringDir({0.0f, 1.0f, 0.0f});
        const float r = std::sqrt(randf()) * anchor.radius;
        const glm::vec3 base = anchor.pos + dir * r;
        spawnSprite(base + glm::vec3{0.0f, randRange(4.0f, 16.0f), 0.0f},
                    dir * randRange(4.0f, 28.0f) + glm::vec3{0.0f, randRange(45.0f, 95.0f), 0.0f},
                    randRange(anchor.radius * 0.035f, anchor.radius * 0.070f),
                    {1.0f, randRange(0.24f, 0.46f), 0.04f, randRange(0.58f, 0.82f)},
                    randRange(0.42f, 0.85f),
                    randRange(0.7f, 1.4f),
                    randRange(-2.8f, 2.8f),
                    randf(),
                    56,
                    8,
                    17.0f,
                    4,
                    randRange(1.0f, 2.1f),
                    0.30f,
                    0.65f);
    }

    while (anchor.smokeAccumulator >= 1.0f) {
        anchor.smokeAccumulator -= 1.0f;
        const glm::vec3 dir = ringDir({0.0f, 1.0f, 0.0f});
        spawnSprite(anchor.pos + dir * (std::sqrt(randf()) * anchor.radius * 0.85f) + glm::vec3{0.0f, 28.0f, 0.0f},
                    glm::vec3{randRange(-22.0f, 22.0f), randRange(55.0f, 120.0f), randRange(-22.0f, 22.0f)},
                    randRange(anchor.radius * 0.11f, anchor.radius * 0.18f),
                    {0.035f, 0.030f, 0.026f, randRange(0.24f, 0.38f)},
                    randRange(1.7f, 2.8f),
                    randRange(0.45f, 0.75f),
                    randRange(-0.55f, 0.55f),
                    randf(),
                    16,
                    8,
                    8.0f,
                    2,
                    1.0f,
                    0.86f,
                    0.25f);
    }

    while (anchor.emberAccumulator >= 1.0f) {
        anchor.emberAccumulator -= 1.0f;
        spawnDebris(anchor.pos + ringDir({0.0f, 1.0f, 0.0f}) * (randf() * anchor.radius),
                    glm::vec3{randRange(-45.0f, 45.0f), randRange(95.0f, 210.0f), randRange(-45.0f, 45.0f)},
                    randRange(0.8f, 1.8f),
                    {1.0f, 0.44f, 0.08f, 1.0f},
                    randRange(0.7f, 1.5f),
                    randRange(-70.0f, 100.0f),
                    0.9f,
                    randRange(1.8f, 4.2f));
    }
}

void ExplosionVfxEffect::update(float dt, Registry& registry, glm::vec3 camPos, glm::vec3 camForward)
{
    groundFire_.erase(std::remove_if(groundFire_.begin(),
                                     groundFire_.end(),
                                     [&](const GroundFireAnchor& a) {
                                         return a.remaining <= 0.0f || !registry.valid(a.entity) ||
                                                !registry.all_of<FireField>(a.entity);
                                     }),
                      groundFire_.end());

    for (GroundFireAnchor& anchor : groundFire_) {
        tickGroundFire(anchor, dt);
    }

    spritePool_.update([&](VfxSpriteParticle& p) -> bool {
        p.age.x += dt;
        if (p.age.x >= p.age.y)
            return false;
        p.pos += p.vel * dt;
        p.vel *= std::max(0.0f, 1.0f - dt * 0.42f);
        p.size += p.size * p.age.z * dt;
        p.rotation += p.age.w * dt;
        const float t = glm::clamp(p.age.x / p.age.y, 0.0f, 1.0f);
        if (p.anim.w < 0.5f) {
            p.color.a = (1.0f - t) * (1.0f - t);
        } else if (p.anim.w < 1.5f) {
            p.color.a = glm::smoothstep(0.0f, 0.12f, t) * (1.0f - glm::smoothstep(0.64f, 1.0f, t));
        } else if (p.anim.w < 2.5f) {
            p.color.a *= 0.995f;
        } else if (p.anim.w < 3.5f) {
            p.color.a = 1.0f - glm::smoothstep(0.1f, 1.0f, t);
        } else {
            p.color.a = glm::smoothstep(0.0f, 0.12f, t) * (1.0f - glm::smoothstep(0.72f, 1.0f, t));
        }
        return p.color.a > 0.01f;
    });

    debrisPool_.update([&](VfxDebrisParticle& p) -> bool {
        p.sim.x += dt;
        if (p.sim.x >= p.sim.y)
            return false;
        p.vel.y -= p.sim.z * dt;
        p.vel *= std::max(0.0f, 1.0f - p.sim.w * dt);
        p.pos += p.vel * dt;
        const float t = glm::clamp(p.sim.x / p.sim.y, 0.0f, 1.0f);
        p.color.a = (1.0f - t) * (1.0f - t * 0.65f);
        return true;
    });

    const uint32_t liveDecals = std::min(decalHead_, k_maxDecals);
    for (uint32_t i = 0; i < liveDecals; ++i) {
        decals_[i].opacity = std::max(0.0f, decals_[i].opacity - dt / 18.0f);
    }

    sortedSprites_.resize(spritePool_.liveCount());
    std::copy(spritePool_.rawData(), spritePool_.rawData() + spritePool_.liveCount(), sortedSprites_.begin());
    std::sort(sortedSprites_.begin(), sortedSprites_.end(), [&](const VfxSpriteParticle& a, const VfxSpriteParticle& b) {
        const float pa = a.shape.w;
        const float pb = b.shape.w;
        if (std::abs(pa - pb) > 0.001f)
            return pa < pb;
        return glm::dot(a.pos - camPos, camForward) > glm::dot(b.pos - camPos, camForward);
    });
}
