#pragma once

#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// GPU-uploadable particle structs.
// All structs must satisfy std430 alignment (vec4 fields keep things clean).
// ---------------------------------------------------------------------------

/// @brief Single billboard particle (sparks, impact flash, shockwave ring).
struct BillboardParticle
{
    glm::vec3 pos;
    float size;      ///< Half-extent of the camera-facing quad.
    glm::vec4 color; ///< RGBA; alpha used by additive blend for brightness.
    glm::vec3 vel;   ///< CPU-simulated velocity (gravity applied per frame).
    float lifetime;  ///< Seconds remaining (particle dies when <= 0).
};
static_assert(sizeof(BillboardParticle) == 48);

/// @brief Oriented capsule streak for fast-bullet tracers (R301 style).
struct TracerParticle
{
    glm::vec3 tip;       ///< World-space front of streak (current bullet pos).
    float radius;        ///< Cross-section half-width (~0.6 units).
    glm::vec3 tail;      ///< World-space back (tip - normalize(vel) * streakLen).
    float brightness;    ///< 1.0→0.0 fade at end of life.
    glm::vec4 coreColor; ///< Bright yellow-white.
    glm::vec4 edgeColor; ///< Orange, alpha=0 at edge.
    float lifetime;
    float _pad[3];
};
static_assert(sizeof(TracerParticle) == 80);

/// @brief Ribbon vertex (pre-expanded on CPU, uploaded as flat vertex stream).
struct RibbonVertex
{
    glm::vec3 pos;
    float _p;
    glm::vec4 color; ///< Pre-multiplied alpha.
};
static_assert(sizeof(RibbonVertex) == 32);

/// @brief Hitscan energy beam (main glowing quad).
struct HitscanBeam
{
    glm::vec3 origin;    ///< Muzzle world position.
    float radius;        ///< Half-width (~1.5 units).
    glm::vec3 hitPos;    ///< Impact world position.
    float lifetime;      ///< Fades quadratically over ~0.12 s.
    glm::vec4 coreColor; ///< Cyan-white: {0.5, 0.9, 1.0, 1.0}.
    glm::vec4 edgeColor; ///< Deep blue:  {0.0, 0.3, 0.8, 0.0}.
};
static_assert(sizeof(HitscanBeam) == 64);

/// @brief Lightning arc vertex (pre-expanded triangle strip, uploaded as flat stream).
struct ArcVertex
{
    glm::vec3 pos;
    float edge; ///< 0 = centerline, ±1 = outer edge (drives glow falloff).
    glm::vec4 color;
};
static_assert(sizeof(ArcVertex) == 32);

/// @brief Smoke / fire billboard (uses noise texture for volumetric look).
struct SmokeParticle
{
    glm::vec3 pos;
    float size;          ///< Grows from 30 → 120 units over lifetime.
    glm::vec4 color;     ///< Pre-multiplied alpha; fades 0→0.35→0.
    float rotation;      ///< Slow random spin (0.1–0.3 rad/s).
    float normalizedAge; ///< 0 (just spawned) → 1 (about to die).
    float maxLifetime;   ///< 3–5 s.
    float _pad;
};
static_assert(sizeof(SmokeParticle) == 48);

/// @brief World-space decal instance (bullet hole, scorch mark).
struct DecalInstance
{
    glm::vec3 pos;   ///< World-space centre.
    float size;      ///< Half-extent in world units (~4).
    glm::vec3 right; ///< World-space tangent (derived from hit normal).
    float _p0;
    glm::vec3 up;    ///< World-space bitangent.
    float opacity;   ///< Fades 1.0→0.0 over ~15 s.
    glm::vec2 uvMin; ///< Atlas UV min.
    glm::vec2 uvMax; ///< Atlas UV max.
};
static_assert(sizeof(DecalInstance) == 64);

/// @brief SDF glyph quad (world-space or screen-space HUD).
struct SdfGlyphGPU
{
    glm::vec3 worldPos; ///< Bottom-left corner (world or pixel space).
    float size;         ///< Glyph height in world units / pixels.
    glm::vec2 uvMin;
    glm::vec2 uvMax;
    glm::vec4 color;
    glm::vec3 right; ///< World-space right (camRight for world, {1,0,0} for HUD).
    float _p0;
    glm::vec3 up;    ///< World-space up   (camUp   for world, {0,1,0} for HUD).
    float _p1;
};
static_assert(sizeof(SdfGlyphGPU) == 80);
