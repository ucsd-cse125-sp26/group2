# Beam VFX — design spec

**Status:** Draft — pending user review
**Date:** 2026-04-23
**Author:** design conversation (Claude)
**Scope:** New reusable, color-parameterized hitscan **beam VFX** system that
produces AAA-quality stylized-realistic beams similar to Hovl Studio's
Unity packs (*AAA Projectiles Vol 2*, *3D Lasers Pack*). Ships a green-plasma
preset as the first consumer, replacing the `RailGun` weapon's lightning-arc
visual. Designed for future expansion to other preset colors/shapes without
shader rewrites.

---

## 1. Goal

Deliver an instant hitscan beam effect that:

- Looks convincingly AAA: multi-layer additive glow, HDR core that blooms,
  macro wavy shape, sub-beam shimmer, symmetric muzzle flare + impact burst.
- Is **reusable**: a single `BeamPreset` struct parameterizes color, thickness,
  wave amplitude/frequency, noise intensity, and lifetime. Any future weapon
  can get a distinct look by authoring one preset.
- Integrates cleanly into the existing `ParticleSystem` + `ParticleRenderer`
  pipeline without touching the core renderer or network layer.
- Runs identically on all existing backends (Metal, D3D12, Vulkan, WebGPU).

## 2. Non-goals

Explicitly out of scope for v1:

- **Traveling projectile variant** — only instant hitscan. A future `ProjectileBeam`
  effect may share the shader but is not part of this spec.
- **Dynamic muzzle/impact point lights** — deferred until the renderer has
  dynamic light support; current pipeline has none.
- **Multiple presets in v1** — only `GreenPlasma` ships. `RedLaser`/`BlueIon`
  stubs exist to validate API shape but are not tuned for release.
- **Texture-based noise** — procedural fBm only (deterministic cross-backend,
  zero texture pipeline changes).
- **Removal of the existing `HitscanEffect` lightning arc** — it stays in the
  codebase, just no longer invoked by `RailGun`. Future cleanup pass.
- **Continuous / held beams** — no hold-to-fire laser; one-shot per trigger.

## 3. Visual anatomy (the AAA recipe)

The effect is a composite of 8 layered elements rendered additively into the
HDR target (`R16G16B16A16_FLOAT`). Bloom post-process gives it the glow pop.

| # | Layer          | Technique                                      | Purpose                          |
|---|----------------|------------------------------------------------|----------------------------------|
| 1 | Core beam      | Ribbon strip, narrow exp falloff, HDR color    | White-hot center                 |
| 2 | Inner glow     | Same strip, wider exp falloff, glow color      | Saturated color halo             |
| 3 | Outer haze     | Same strip, widest falloff, low intensity      | Feeds bloom, soft corona         |
| 4 | Macro wave     | CPU-baked sine displacement on spine           | The visible "S" curve            |
| 5 | Micro shimmer  | fBm in fragment shader, scrolling over time    | "Alive plasma" feel              |
| 6 | Muzzle flare   | Billboard flash quad + 8–12 radial sparks      | Shot-origin emphasis             |
| 7 | Impact flash   | Billboard flash quad, scale-and-fade curve     | Hit-point punch                  |
| 8 | Impact sparks  | 20–30 billboards in hemisphere about normal    | Debris feel                      |

Lifetime is ~220 ms total with a non-linear curve:
**10 % ramp-in → 40 % hold → 50 % decay.**

## 4. Architecture

### 4.1 New files

```
src/client/particles/effects/BeamEffect.hpp   // class + active-beam pool
src/client/particles/effects/BeamEffect.cpp
src/client/particles/BeamPresets.hpp          // preset catalog (declarations)
src/client/particles/BeamPresets.cpp          // preset catalog (definitions)
shaders/beam.vert                             // pass-through + uniform plumbing
shaders/beam.frag                             // 3-layer glow + fBm shimmer
```

### 4.2 Modified files

```
src/client/particles/ParticleSystem.{hpp,cpp}     // owns BeamEffect, new spawnBeam()
src/client/particles/ParticleRenderer.{hpp,cpp}   // new beam pipeline + upload path
src/client/particles/effects/ImpactEffect.{hpp,cpp}  // new spawnBeamImpact(pos, normal, preset)
src/client/game/Game.cpp                          // RailGun fire path calls spawnBeam()
src/client/debug/DebugUI.cpp                      // live-tuning panel for preset
cmake/CompileShaders.cmake (or equivalent)        // compile beam.vert/frag
```

### 4.3 Key types

```cpp
// BeamEffect.hpp
struct BeamPreset {
    glm::vec3 coreColorHDR;      // e.g., (0.9, 4.0, 0.9) — blooms white-green
    glm::vec3 glowColorHDR;      // e.g., (0.1, 2.5, 0.2) — saturated green halo
    float     coreThickness;     // meters, e.g., 0.04
    float     glowThickness;     // meters, e.g., 0.15  (must be > coreThickness)
    float     waveAmplitude;     // meters, macro displacement, e.g., 0.12
    float     waveFrequency;     // cycles per meter, e.g., 0.35
    float     waveSeedRange;     // phase randomization per shot, e.g., 6.2832
    float     noiseScrollSpeed;  // fragment noise scroll, e.g., 2.0
    float     noiseIntensity;    // [0,1] shimmer strength, e.g., 0.6
    float     lifetime;          // seconds, e.g., 0.22
    float     muzzleFlareScale;  // meters, e.g., 0.25
    int       impactSparkCount;  // e.g., 24
    float     impactFlashScale;  // meters, e.g., 0.35
};

struct BeamVertex {
    glm::vec3 position;        // world-space, macro displacement baked in
    glm::vec2 uv;              // u = along length [0,1], v = across width [-1,+1]
    float     lifeFrac;        // age/lifetime, for fragment life-fade
    glm::vec3 coreColorHDR;    // copied from preset per vertex
    glm::vec3 glowColorHDR;    // copied from preset per vertex
    float     noiseScrollSpeed;// copied from preset per vertex
    float     noiseIntensity;  // copied from preset per vertex
};
// All per-beam params are carried per-vertex so multiple beams (different
// presets) batch into a single draw call. Cost: ~44 B × 50 verts × 32 beams
// ≈ 70 KB per frame, negligible. Alternative (SSBO indexed by beam id) is a
// future optimization if vertex size becomes a concern.

class BeamEffect {
public:
    void   spawn(glm::vec3 origin, glm::vec3 hit, const BeamPreset& preset);
    void   update(float dt);
    void   buildGeometry(std::vector<BeamVertex>& verts, const Camera& cam);
    size_t activeCount() const;
private:
    struct ActiveBeam {
        glm::vec3 origin, hit;
        float     age;
        float     waveSeed;
        const BeamPreset* preset;  // non-owning; presets are static-lifetime
    };
    std::vector<ActiveBeam> beams_;
    static constexpr size_t kMaxActive = 32;  // evict-oldest on full
    static constexpr int    kSegments  = 24;  // spine subdivisions per beam
};
```

### 4.4 Public API on `ParticleSystem`

```cpp
void spawnBeam(glm::vec3 origin, glm::vec3 hit, const BeamPreset& preset);
void spawnBeam(glm::vec3 origin, glm::vec3 hit, WeaponType wt); // convenience
```

### 4.5 Preset catalog

```cpp
namespace beam_presets {
    extern const BeamPreset k_greenPlasma;  // v1 ships this — matches reference
    extern const BeamPreset k_redLaser;     // stub for API validation
    extern const BeamPreset k_blueIon;      // stub for API validation
}
const BeamPreset& beamPresetFor(WeaponType wt);  // WeaponType -> preset lookup
```

## 5. Rendering pipeline

### 5.1 Geometry — camera-facing ribbon strip

- Each beam is a triangle strip of `(kSegments + 1) × 2 = 50` vertices.
- Each pair of vertices (a "ring") is offset left/right from the displaced
  spine by `±glowThickness` along a width axis that always faces the camera.
- Width axis per ring is computed per-segment as:
  ```
  toCam        = normalize(camPos - ringCenter)
  spineTangent = normalize(nextRingCenter - prevRingCenter)
  widthAxis    = normalize(cross(spineTangent, toCam))
  ```
  This keeps the ribbon broadside to the viewer at every point along the curve,
  regardless of viewing angle.
- **Degenerate case:** when `abs(dot(spineTangent, toCam)) > 0.99` (viewer
  sighting straight down the beam), the cross collapses. Fallback to world-up:
  `widthAxis = normalize(cross(spineTangent, vec3(0,1,0)))`, or world-right if
  the beam itself is near-vertical.
- Macro sine displacement is **baked CPU-side** so geometry is stable for the
  beam's lifetime (curve doesn't wiggle during the 220 ms).
- Multiple active beams joined with degenerate triangles into one strip for a
  single draw call regardless of which presets are active (all per-beam state
  is carried per-vertex — see §4.3).
- Worst-case: 32 beams × 50 verts = 1600 verts/frame. Negligible.

### 5.2 Pipeline state

| Setting       | Value                                           |
|---------------|-------------------------------------------------|
| Topology      | `TRIANGLE_STRIP`                                |
| Color target  | Main HDR `R16G16B16A16_FLOAT`                   |
| Blend         | `SRC_ALPHA × ONE` (additive)                    |
| Depth test    | Yes                                             |
| Depth write   | No                                              |
| Cull          | None                                            |
| MSAA          | Match scene                                     |

### 5.3 Upload & draw path

```text
ParticleSystem::update(dt)
  └─ beamEffect_.update(dt)                 // age, cull dead
  └─ beamEffect_.buildGeometry(verts, cam)  // CPU sine + camera-perpendicular rings

ParticleRenderer::renderBeams(cmdBuf, pass)
  └─ uploadBeamVerts(verts)                 // staging → GPU copy
  └─ bindBeamPipeline()
  └─ SDL_DrawGPUPrimitives(vertCount, 1, 0, 0)
```

Muzzle flare and impact sparks piggy-back on the existing `particle_billboard`
pipeline — no new billboard infrastructure.

## 6. Shader design

### 6.1 `beam.vert` — pass-through

```glsl
#version 450
layout(location=0) in vec3  aPos;
layout(location=1) in vec2  aUV;
layout(location=2) in float aLifeFrac;
layout(location=3) in vec3  aCoreColor;
layout(location=4) in vec3  aGlowColor;
layout(location=5) in float aNoiseScrollSpeed;
layout(location=6) in float aNoiseIntensity;

layout(set=1, binding=0) uniform BeamUniforms {
    mat4  viewProj;
    float time;   // seconds, global
};

layout(location=0) out vec2  vUV;
layout(location=1) out float vLifeFrac;
layout(location=2) out vec3  vCoreColor;
layout(location=3) out vec3  vGlowColor;
layout(location=4) out float vNoiseScrollSpeed;
layout(location=5) out float vNoiseIntensity;
layout(location=6) out float vTime;

void main() {
    gl_Position        = viewProj * vec4(aPos, 1.0);
    vUV                = aUV;
    vLifeFrac          = aLifeFrac;
    vCoreColor         = aCoreColor;
    vGlowColor         = aGlowColor;
    vNoiseScrollSpeed  = aNoiseScrollSpeed;
    vNoiseIntensity    = aNoiseIntensity;
    vTime              = time;
}
```

### 6.2 `beam.frag` — the visual heart

Six responsibilities:

1. `d = |vUV.y|` → signed distance from beam centerline.
2. Three exponential falloffs composed additively: `core`, `inner`, `outer`.
3. fBm noise over `(u * 6 - time * vNoiseScrollSpeed, vLifeFrac * 10)` →
   `shimmer`, modulated by `vNoiseIntensity`.
4. `endFade` — `smoothstep(0, 0.05, u) * smoothstep(1, 0.95, u)` to avoid hard
   cuts at origin/hit.
5. `life` curve: 10 % ramp / 40 % hold / 50 % decay.
6. Composite: `vCoreColor × core × 2.5 + vGlowColor × inner × 1.2 +
   vGlowColor × outer × 0.35`, multiplied by `shimmer × endFade × life`,
   output as RGB; alpha=1.0 (additive blend ignores alpha).

All per-beam parameters (`vCoreColor`, `vGlowColor`, `vNoiseScrollSpeed`,
`vNoiseIntensity`) arrive as varyings from the vertex stage, enabling a single
draw call to batch beams of mixed presets.

Full source is included in the implementation plan.

### 6.3 Why procedural noise (not a texture)

- Deterministic across Metal / D3D12 / Vulkan / WebGPU — no backend-specific
  texture-format surprises.
- Zero pipeline plumbing — no new texture binding, no staging upload.
- Per-fragment cost is ~30 ALU for 3-octave fBm, trivial at the beam's ~50 k
  fragment footprint.
- Texture swap remains a future option (reserve `set=3, binding=1` for noise
  texture if ever needed).

## 7. Integration

### 7.1 Weapon fire path

`Game.cpp` currently does, for any hitscan weapon:

```cpp
particleSystem.spawnHitscanBeam(hip, hitPos, currentEquippedType_);
particleSystem.spawnImpactEffect(hitPos, hitNormal, hitSurface, currentEquippedType_);
```

For a weapon whose `WeaponType` maps to a beam preset (v1: `RailGun`), switch to:

```cpp
const BeamPreset& preset = beamPresetFor(currentEquippedType_);
particleSystem.spawnBeam(hip, hitPos, preset);
particleSystem.spawnBeamImpact(hitPos, hitNormal, preset);
```

`WeaponType::RailGun` switches visuals; other weapons unchanged.

### 7.2 Muzzle flare

Spawned inside `BeamEffect::spawn()` as a self-contained side-effect so callers
need only the two `spawnBeam*` lines. Uses existing billboard pipeline.

### 7.3 Impact burst

New overload `ImpactEffect::spawnBeamImpact(pos, normal, preset)`:

```cpp
// 1. Flash quad: scale 0→1.5→0.8→0 over 180 ms, color = glowColor × 6
// 2. Radial sparks: preset.impactSparkCount billboards, random hemisphere(normal)
//    speed 5–10 m/s, life 300–600 ms, color = glowColorHDR
// 3. Smoke puff: smokeEffect_.spawn(pos, normal) if surface is solid
// 4. Scorch decal: decalSystem_.spawn(pos, normal, DecalType::Scorch)
```

### 7.4 Remote player beams

`WeaponFiredEvent` handler at `Game.cpp:148` uses the same spawn path as local
fire — no network changes required.

## 8. Testing & tuning

### 8.1 Unit tests (mechanical)

- `spawn()` adds exactly one `ActiveBeam`; at `kMaxActive`, oldest is evicted.
- `update(dt)` removes beams whose `age > preset->lifetime`.
- `buildGeometry()` emits `(kSegments + 1) × 2` vertices per active beam.
- Preset validation asserts: `coreThickness < glowThickness`,
  `waveAmplitude >= 0`, `lifetime > 0`, `impactSparkCount >= 0`.

### 8.2 Visual tuning (mandatory — VFX cannot be tuned in code alone)

Add to existing `DebugUI.cpp` alongside the impact test at line 850:

```
[Spawn test beam]  — fires beam from camera to 30 m ahead
Preset [GreenPlasma ▾]
── Live sliders (mutate a local preset copy) ──
Core color RGB
Glow color RGB
Core / glow thickness
Wave amplitude / frequency
Noise intensity / scroll speed
Lifetime
Muzzle flare scale
Impact spark count / flash scale
```

Sliders write to a local `BeamPreset` the debug panel owns; satisfied values
are hand-copied into `BeamPresets.cpp::beam_presets::k_greenPlasma`.

### 8.3 Reference comparison

After tuning, take a screenshot against a dark wall and compare to the Hovl
reference image. Checklist:

- [ ] White-hot center visible (core saturates to white in bloom)
- [ ] Saturated green corona surrounds the core
- [ ] Macro "S" wave visible along length
- [ ] Shimmer animates during the 220 ms life
- [ ] Muzzle flare pulses at origin
- [ ] Impact sparks spray from hit point in hemisphere
- [ ] Scorch decal persists on surface

### 8.4 Cross-backend verification

Run the test beam on each backend and confirm pixel-similar output (small
sub-pixel differences acceptable from rasterizer precision; structural
differences are bugs).

- Metal (macOS)
- D3D12 (Windows)
- Vulkan (Linux)
- WebGPU (web build)

## 9. Performance budget

| Cost                | Value                                             |
|---------------------|---------------------------------------------------|
| Active beam cap     | 32 concurrent                                     |
| Vertices / frame    | ≤ 1600                                            |
| Fragments / beam    | ~50 wide × ~1000 long ≈ 50 000 (bounded by len)   |
| Per-frag ALU        | ~30 (3-octave fBm + 3 exp + composite)            |
| Draw calls / frame  | 1 for beams + existing billboard draws for bursts |
| Memory              | `sizeof(BeamVertex) × 1600` ≈ 76 KB staging buf   |

At 8 LAN players × 10 shots/s × 220 ms life ≈ 17.6 concurrent beams average,
well under the cap.

## 10. Risks & mitigations

| Risk                                      | Mitigation                                     |
|-------------------------------------------|------------------------------------------------|
| Hash-based noise banding on some GPUs     | Swap to 64×64 tileable noise texture (reserved binding) |
| Bloom too aggressive → washes out core    | Lower `coreColorHDR` intensity; post-bloom tonemap adjustment |
| Camera-facing ribbon flips at exact edge-on viewing | Fallback: if `abs(dot(beamDir, viewDir)) > 0.99`, use world-up axis |
| Additive blending stacks to white on overlap | Acceptable for rare overlap; cap kMaxActive already limits |
| WebGPU backend differs in precision       | Cross-backend check in §8.4 catches pre-merge  |

## 11. Out of scope / future work

- Continuous held-beam variant (pulse + sustain + release).
- Traveling projectile that leaves a beam trail (new `ProjectileBeam` effect).
- Dynamic lights at muzzle / impact.
- Noise texture swap if banding is observed.
- Additional curated presets (`RedLaser`, `BlueIon`, `PurpleVoid`, etc.).
- Removal of unused `HitscanEffect` lightning-arc code after preset migration.
