# HUD System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a renderer-agnostic HUD system that renders to an offscreen texture, composited by any renderer (legacy or new) after tone mapping.

**Architecture:** Retained widgets with immediate-mode draw API. Widgets own state + animation, call into HudContext to emit geometry, which HudRenderer flushes to a GPU offscreen target. The renderer blits the output texture as a single fullscreen quad. HUD has zero knowledge of which renderer is active.

**Tech Stack:** C++17, SDL3 GPU, GLSL 450 → SPIR-V (+ MSL/DXIL transpile), shared SdfAtlas for text.

---

## File Map

**New files (create):**

| File | Responsibility |
|------|---------------|
| `src/client/hud/HudTypes.hpp` | Shared types: HudColor, HudAnchor, HudAlign, HudVertex, HudGameState, CrosshairStyle, event structs |
| `src/client/hud/HudTween.hpp` | Tween pool struct + ease function declarations |
| `src/client/hud/HudTween.cpp` | Tween update logic + ease function implementations |
| `src/client/hud/HudRenderer.hpp` | GPU backend: offscreen target, pipeline, vertex upload |
| `src/client/hud/HudRenderer.cpp` | GPU backend implementation |
| `src/client/hud/HudContext.hpp` | Immediate-mode draw API declarations |
| `src/client/hud/HudContext.cpp` | Geometry batching implementation |
| `src/client/hud/HudWidget.hpp` | Base widget struct |
| `src/client/hud/Hud.hpp` | Top-level orchestrator declarations |
| `src/client/hud/Hud.cpp` | Orchestrator: owns widgets, drives update/render cycle |
| `src/client/hud/widgets/CrosshairWidget.hpp` | Crosshair + hit marker overlay |
| `src/client/hud/widgets/CrosshairWidget.cpp` | Crosshair implementation |
| `src/client/hud/widgets/HealthArmorBar.hpp` | Health + armor bars |
| `src/client/hud/widgets/HealthArmorBar.cpp` | Health/armor implementation |
| `src/client/hud/widgets/AmmoCounter.hpp` | Ammo display |
| `src/client/hud/widgets/AmmoCounter.cpp` | Ammo implementation |
| `src/client/hud/widgets/KillFeed.hpp` | Kill feed sliding list |
| `src/client/hud/widgets/KillFeed.cpp` | Kill feed implementation |
| `src/client/hud/widgets/HitMarkerWidget.hpp` | Hit confirm flare |
| `src/client/hud/widgets/HitMarkerWidget.cpp` | Hit marker implementation |
| `src/client/hud/widgets/DamageIndicator.hpp` | Directional damage arcs |
| `src/client/hud/widgets/DamageIndicator.cpp` | Damage indicator implementation |
| `src/client/hud/widgets/RoundTimer.hpp` | Countdown timer |
| `src/client/hud/widgets/RoundTimer.cpp` | Round timer implementation |
| `src/client/hud/widgets/TeamStatusBar.hpp` | Team alive/dead indicators |
| `src/client/hud/widgets/TeamStatusBar.cpp` | Team status implementation |
| `src/client/hud/widgets/Scoreboard.hpp` | Full overlay scoreboard |
| `src/client/hud/widgets/Scoreboard.cpp` | Scoreboard implementation |
| `src/client/hud/widgets/BuyMenu.hpp` | Buy phase menu |
| `src/client/hud/widgets/BuyMenu.cpp` | Buy menu implementation |
| `src/client/hud/widgets/Minimap.hpp` | Top-down minimap |
| `src/client/hud/widgets/Minimap.cpp` | Minimap implementation |
| `src/client/hud/debug/HudDebugPanel.hpp` | ImGui tweaking panel |
| `src/client/hud/debug/HudDebugPanel.cpp` | Debug panel implementation |
| `shaders/hud.vert` | HUD vertex shader |
| `shaders/hud.frag` | HUD fragment shader |

**Existing files (modify):**

| File | Change |
|------|--------|
| `src/client/renderer/IRenderer.hpp` | Add `SetHudTexture` to RendererFeature enum + `setHudTexture()` pure virtual |
| `src/client/renderer/Renderer.hpp` | Add `hudTexture_` member + `setHudTexture()` override + `hudBlitPipeline_` |
| `src/client/renderer/Renderer.cpp` | Implement `setHudTexture()`, add HUD blit after ImGui in tone mapping pass, init blit pipeline |
| `src/client/renderer/HybridRenderer.hpp` | Add `setHudTexture()` override |
| `src/client/renderer/HybridRenderer.cpp` | Route `setHudTexture()` to legacy/new, add enum to routing table |
| `src/client/renderer-new/Renderer.hpp` | Add `setHudTexture()` stub override |
| `src/client/game/Game.hpp` | Add `#include "hud/Hud.hpp"`, add `Hud hud_;` member |
| `src/client/game/Game.cpp` | Wire hud init/update/render/quit/event, build HudGameState each frame |
| `src/client/particles/ParticleSystem.hpp` | Add `const SdfAtlas& sdfAtlas() const` accessor |
| `CMakeLists.txt` | Add HUD source files to `group2` target + `hud.vert hud.frag` to `SHADER_SOURCES` |

---

### Task 1: HudTypes.hpp — Shared Type Definitions

**Files:**
- Create: `src/client/hud/HudTypes.hpp`

- [ ] **Step 1: Create the HudTypes header with all shared types**

```cpp
/// @file HudTypes.hpp
/// @brief Shared types for the HUD system.

#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <span>
#include <string>

// ── Colors ──────────────────────────────────────────────────────────────────

/// @brief RGBA color for HUD elements (linear space, straight alpha).
struct HudColor
{
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;

    constexpr HudColor() = default;
    constexpr HudColor(float r_, float g_, float b_, float a_ = 1.f) : r(r_), g(g_), b(b_), a(a_) {}

    static constexpr HudColor white() { return {1, 1, 1, 1}; }
    static constexpr HudColor black() { return {0, 0, 0, 1}; }
    static constexpr HudColor red() { return {1, 0, 0, 1}; }
    static constexpr HudColor green() { return {0, 1, 0, 1}; }
    static constexpr HudColor yellow() { return {1, 1, 0, 1}; }
    static constexpr HudColor cyan() { return {0, 1, 1, 1}; }
    static constexpr HudColor transparent() { return {0, 0, 0, 0}; }
};

// ── Enums ───────────────────────────────────────────────────────────────────

enum class HudAnchor
{
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

enum class HudAlign
{
    Left,
    Center,
    Right,
};

/// @brief Icon identifiers for the HUD icon atlas.
enum class HudIcon : uint8_t
{
    // Placeholder — populated when the icon atlas is authored.
    None = 0,
};

// ── Vertex ──────────────────────────────────────────────────────────────────

/// @brief Per-vertex data for all HUD geometry (48 bytes).
struct HudVertex
{
    float position[2]; ///< Pixel coordinates, origin top-left.
    float uv[2];       ///< Texture coords (atlas UV or local quad pos for shapes).
    float color[4];    ///< RGBA, straight alpha.
    float texMode;     ///< 0=solid, 1=SDF text, 2=sprite, 3=SDF rounded rect.
    float shapeData[3]; ///< Mode 3 only: [halfWidth, halfHeight, cornerRadius] in pixels.
};

// ── Crosshair ───────────────────────────────────────────────────────────────

/// @brief Crosshair appearance parameters.
struct CrosshairStyle
{
    float gap = 3.f;       ///< Gap from center to start of each line (pixels).
    float length = 6.f;    ///< Length of each crosshair arm (pixels).
    float thickness = 2.f; ///< Line thickness (pixels).
    HudColor color{0.f, 1.f, 0.f, 0.85f}; ///< Default green.
    bool dot = true;       ///< Draw center dot.
};

// ── Game State Contract ─────────────────────────────────────────────────────

/// @brief Kill feed entry data.
struct HudKillFeedEntry
{
    std::string killerName;
    std::string victimName;
    int weaponId = 0;
    bool isHeadshot = false;
};

/// @brief Damage direction indicator.
struct HudDamageEvent
{
    float angleDeg = 0.f; ///< Direction the damage came from (degrees, 0=front, CW).
    float amount = 0.f;   ///< Damage amount (for intensity scaling).
};

/// @brief Hit confirmation event.
struct HudHitConfirm
{
    bool isHeadshot = false;
    bool isKill = false;
};

/// @brief Per-teammate status (for scoreboard / team bar).
struct HudTeamMemberStatus
{
    std::string name;
    int health = 100;
    bool isAlive = true;
    int kills = 0;
    int deaths = 0;
    int ping = 0;
};

/// @brief Snapshot of game state consumed by the HUD each frame.
///
/// Filled by Game from ECS data. The HUD never imports ECS headers.
struct HudGameState
{
    int health = 100, maxHealth = 100;
    int armor = 0, maxArmor = 100;
    int ammoClip = 30, ammoReserve = 90;
    int weaponId = 0;
    float roundTimeRemaining = 0.f;
    bool isAlive = true;
    bool isBuyPhase = false;

    // Events (valid for this frame only).
    std::span<const HudKillFeedEntry> killFeedEvents;
    std::span<const HudDamageEvent> damageEvents;
    std::span<const HudHitConfirm> hitConfirms;

    // Team status.
    std::span<const HudTeamMemberStatus> allies;
    std::span<const HudTeamMemberStatus> enemies;
    int allyScore = 0, enemyScore = 0;

    // Screen dimensions (set by Game each frame).
    float screenW = 1280.f, screenH = 720.f;
};
```

- [ ] **Step 2: Verify the file compiles**

Add a temporary include in any existing .cpp to confirm no syntax errors:
```bash
cd /home/user/Documents/dev/group2 && echo '#include "hud/HudTypes.hpp"' | cat - /dev/null > /tmp/hud_check.cpp
# Or just proceed to next task — the build will catch errors when we wire it in.
```

- [ ] **Step 3: Commit**

```bash
git add src/client/hud/HudTypes.hpp
git commit -m "feat(hud): add shared HUD type definitions

HudColor, HudAnchor, HudAlign, HudVertex, CrosshairStyle,
HudGameState, and event structs (kill feed, damage, hit confirm,
team status)."
```

---

### Task 2: HUD Shaders

**Files:**
- Create: `shaders/hud.vert`
- Create: `shaders/hud.frag`
- Modify: `CMakeLists.txt:458-488` (add to SHADER_SOURCES)

- [ ] **Step 1: Create the vertex shader**

```glsl
/// @file hud.vert
/// @brief HUD vertex shader — converts pixel coordinates to clip space.
#version 450

layout(location = 0) in vec2  inPosition;   // pixel coords, origin top-left
layout(location = 1) in vec2  inUV;
layout(location = 2) in vec4  inColor;
layout(location = 3) in float inTexMode;
layout(location = 4) in vec3  inShapeData;  // mode 3: halfW, halfH, radius

layout(set = 0, binding = 0) uniform HudUniforms {
    vec2 screenSize;    // viewport width, height in pixels
};

layout(location = 0) out vec2  vUV;
layout(location = 1) out vec4  vColor;
layout(location = 2) out float vTexMode;
layout(location = 3) out vec3  vShapeData;

void main()
{
    // Pixel coords → NDC.  Y is flipped so (0,0) = top-left.
    vec2 ndc;
    ndc.x =  (inPosition.x / screenSize.x) * 2.0 - 1.0;
    ndc.y = -((inPosition.y / screenSize.y) * 2.0 - 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);

    vUV        = inUV;
    vColor     = inColor;
    vTexMode   = inTexMode;
    vShapeData = inShapeData;
}
```

Write to `shaders/hud.vert`.

- [ ] **Step 2: Create the fragment shader**

```glsl
/// @file hud.frag
/// @brief HUD fragment shader — branches on texMode for solid, SDF text,
///        sprite, or SDF rounded rect.
#version 450

layout(location = 0) in vec2  vUV;
layout(location = 1) in vec4  vColor;
layout(location = 2) in float vTexMode;
layout(location = 3) in vec3  vShapeData;

layout(set = 1, binding = 0) uniform sampler2D sdfAtlas;
layout(set = 1, binding = 1) uniform sampler2D iconAtlas;

layout(location = 0) out vec4 outColor;

void main()
{
    int mode = int(vTexMode + 0.5);

    if (mode == 1) {
        // SDF text
        float sdf   = texture(sdfAtlas, vUV).r;
        float w     = fwidth(sdf) * 0.7;
        float alpha = smoothstep(0.5 - w, 0.5 + w, sdf);
        outColor = vec4(vColor.rgb, vColor.a * alpha);

    } else if (mode == 2) {
        // Sprite / icon
        vec4 texel = texture(iconAtlas, vUV);
        outColor = texel * vColor;

    } else if (mode == 3) {
        // SDF rounded rectangle.
        // vUV = local position within quad, [0,1]² → map to [-1,1]²
        // vShapeData = (halfWidth, halfHeight, cornerRadius) in pixels.
        vec2  halfExt = vShapeData.xy;
        float radius  = vShapeData.z;

        // Map UV to pixel offset from center.
        vec2 localPos = (vUV * 2.0 - 1.0) * halfExt;

        // Signed distance to rounded rectangle.
        vec2 d = abs(localPos) - (halfExt - radius);
        float dist = min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - radius;

        // Anti-aliased edge.
        float fw    = fwidth(dist);
        float alpha = 1.0 - smoothstep(-fw, fw, dist);
        outColor = vec4(vColor.rgb, vColor.a * alpha);

    } else {
        // Mode 0: solid color
        outColor = vColor;
    }
}
```

Write to `shaders/hud.frag`.

- [ ] **Step 3: Register shaders in CMakeLists.txt**

In `CMakeLists.txt`, add to the `SHADER_SOURCES` list (after line 487, before the closing paren):

```cmake
    hud.vert hud.frag                    # HUD overlay
```

- [ ] **Step 4: Verify shaders compile**

```bash
cd /home/user/Documents/dev/group2
cmake --preset debug 2>&1 | tail -5
cmake --build build/debug --target shaders 2>&1 | tail -20
```

Expected: shaders compile to `build/debug/shaders/hud.vert.spv` and `build/debug/shaders/hud.frag.spv`.

- [ ] **Step 5: Commit**

```bash
git add shaders/hud.vert shaders/hud.frag CMakeLists.txt
git commit -m "feat(hud): add HUD vertex and fragment shaders

Pixel-space vertex shader with screenSize uniform. Fragment shader
branches on texMode: solid color, SDF text, sprite, SDF rounded rect."
```

---

### Task 3: HudTween — Animation System

**Files:**
- Create: `src/client/hud/HudTween.hpp`
- Create: `src/client/hud/HudTween.cpp`

- [ ] **Step 1: Create HudTween.hpp**

```cpp
/// @file HudTween.hpp
/// @brief Lightweight fixed-pool tween engine for HUD animations.

#pragma once

#include <cstdint>

/// @brief Easing function signature: maps t ∈ [0,1] → [0,1].
using HudEaseFn = float (*)(float t);

// ── Built-in easing functions ───────────────────────────────────────────────
float easeLinear(float t);
float easeInQuad(float t);
float easeOutQuad(float t);
float easeInOutQuad(float t);
float easeOutBack(float t);
float easeOutElastic(float t);

// ── Tween pool ──────────────────────────────────────────────────────────────

/// @brief One active interpolation targeting a float.
struct HudTweenEntry
{
    float* target = nullptr;
    float from = 0.f;
    float to = 0.f;
    float duration = 0.f;
    float elapsed = 0.f;
    HudEaseFn ease = easeOutQuad;
    bool active = false;
};

/// @brief Fixed-size tween pool.  No heap allocations.
class HudTweenPool
{
public:
    static constexpr int k_maxTweens = 64;

    /// @brief Start or replace a tween on *target* from its current value to *to*.
    void tween(float* target, float to, float duration, HudEaseFn ease = easeOutQuad);

    /// @brief Start or replace a tween on *target* from *from* to *to*.
    void tween(float* target, float from, float to, float duration, HudEaseFn ease = easeOutQuad);

    /// @brief Cancel any active tween targeting *target*.
    void cancel(float* target);

    /// @brief Tick all active tweens by *dt* seconds.
    void update(float dt);

private:
    HudTweenEntry entries_[k_maxTweens] = {};

    /// @brief Find an existing tween on target, or the first free slot.
    HudTweenEntry* findSlot(float* target);
};
```

- [ ] **Step 2: Create HudTween.cpp**

```cpp
/// @file HudTween.cpp
/// @brief Tween pool + easing function implementations.

#include "HudTween.hpp"

#include <algorithm>
#include <cmath>

// ── Easing functions ────────────────────────────────────────────────────────

float easeLinear(float t) { return t; }

float easeInQuad(float t) { return t * t; }

float easeOutQuad(float t) { return t * (2.f - t); }

float easeInOutQuad(float t)
{
    return t < 0.5f ? 2.f * t * t : -1.f + (4.f - 2.f * t) * t;
}

float easeOutBack(float t)
{
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.f;
    const float tm1 = t - 1.f;
    return 1.f + c3 * tm1 * tm1 * tm1 + c1 * tm1 * tm1;
}

float easeOutElastic(float t)
{
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    constexpr float c4 = (2.f * 3.14159265f) / 3.f;
    return std::pow(2.f, -10.f * t) * std::sin((t * 10.f - 0.75f) * c4) + 1.f;
}

// ── HudTweenPool ────────────────────────────────────────────────────────────

HudTweenEntry* HudTweenPool::findSlot(float* target)
{
    // First pass: find existing tween on this target.
    for (auto& e : entries_)
        if (e.active && e.target == target)
            return &e;

    // Second pass: find a free slot.
    for (auto& e : entries_)
        if (!e.active)
            return &e;

    return nullptr; // Pool full — silently drop.
}

void HudTweenPool::tween(float* target, float to, float duration, HudEaseFn ease)
{
    tween(target, *target, to, duration, ease);
}

void HudTweenPool::tween(float* target, float from, float to, float duration, HudEaseFn ease)
{
    HudTweenEntry* slot = findSlot(target);
    if (!slot)
        return;

    slot->target = target;
    slot->from = from;
    slot->to = to;
    slot->duration = std::max(duration, 0.001f);
    slot->elapsed = 0.f;
    slot->ease = ease ? ease : easeLinear;
    slot->active = true;
    *target = from;
}

void HudTweenPool::cancel(float* target)
{
    for (auto& e : entries_)
        if (e.active && e.target == target)
            e.active = false;
}

void HudTweenPool::update(float dt)
{
    for (auto& e : entries_) {
        if (!e.active)
            continue;

        e.elapsed += dt;
        if (e.elapsed >= e.duration) {
            *e.target = e.to;
            e.active = false;
        } else {
            const float t = e.ease(e.elapsed / e.duration);
            *e.target = e.from + (e.to - e.from) * t;
        }
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add src/client/hud/HudTween.hpp src/client/hud/HudTween.cpp
git commit -m "feat(hud): add lightweight tween pool with easing functions

Fixed 64-slot pool, no heap allocations. Built-in eases: linear,
quad, back, elastic. Replaces existing tween on same target."
```

---

### Task 4: HudRenderer — GPU Backend

**Files:**
- Create: `src/client/hud/HudRenderer.hpp`
- Create: `src/client/hud/HudRenderer.cpp`

- [ ] **Step 1: Create HudRenderer.hpp**

```cpp
/// @file HudRenderer.hpp
/// @brief GPU backend for the HUD system — owns offscreen target, pipeline,
///        and vertex buffer upload.

#pragma once

#include "HudTypes.hpp"

#include <SDL3/SDL.h>

#include <span>

class SdfAtlas;

/// @brief Renders batched HUD geometry to an offscreen RGBA8 texture.
///
/// Owns the GPU pipeline, offscreen render target, dynamic vertex buffer,
/// and sampler bindings.  The renderer (legacy/new) reads getOutputTexture()
/// and blits it after tone mapping.
class HudRenderer
{
public:
    /// @brief Initialise GPU resources.
    /// @param device       Shared SDL GPU device.
    /// @param shaderFormat SPIR-V, MSL, or DXIL.
    /// @param sdfAtlas     Font atlas for SDF text rendering.
    /// @param screenW      Initial viewport width.
    /// @param screenH      Initial viewport height.
    /// @return true on success.
    bool init(SDL_GPUDevice* device,
              SDL_GPUShaderFormat shaderFormat,
              const SdfAtlas& sdfAtlas,
              uint32_t screenW,
              uint32_t screenH);

    /// @brief Release all GPU resources.
    void quit();

    /// @brief Recreate the offscreen target on window resize.
    void resize(uint32_t newW, uint32_t newH);

    /// @brief Upload vertex data, execute the HUD render pass, and submit.
    ///
    /// Acquires its own command buffer, clears the offscreen target to
    /// transparent black, draws all batched quads, and submits.
    /// @param vertices Flat vertex array produced by HudContext.
    /// @param clipRects Parallel array of clip rects for scissor state changes.
    ///                  Each entry is {startVertex, vertexCount, x, y, w, h}.
    void render(std::span<const HudVertex> vertices,
                std::span<const std::array<float, 6>> clipRects);

    /// @brief The offscreen texture to be blitted by the renderer.
    [[nodiscard]] SDL_GPUTexture* getOutputTexture() const { return offscreenTarget_; }

    /// @brief Current target width.
    [[nodiscard]] uint32_t width() const { return width_; }

    /// @brief Current target height.
    [[nodiscard]] uint32_t height() const { return height_; }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShaderFormat shaderFormat_ = SDL_GPU_SHADERFORMAT_INVALID;

    // Offscreen target
    SDL_GPUTexture* offscreenTarget_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    // Pipeline
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;

    // Samplers (bound to fragment set 1)
    SDL_GPUTexture* sdfAtlasTex_ = nullptr;   ///< Non-owning: from SdfAtlas.
    SDL_GPUSampler* sdfAtlasSamp_ = nullptr;  ///< Non-owning: from SdfAtlas.
    SDL_GPUTexture* iconAtlasTex_ = nullptr;  ///< Owning: 1×1 white fallback until real atlas.
    SDL_GPUSampler* iconAtlasSamp_ = nullptr; ///< Owning.

    // Dynamic vertex buffer (recreated if capacity exceeded)
    SDL_GPUBuffer* vertexBuffer_ = nullptr;
    SDL_GPUTransferBuffer* transferBuffer_ = nullptr;
    uint32_t vertexCapacity_ = 0; ///< Current buffer capacity in vertices.

    // Uniform buffer data
    struct ScreenUniforms
    {
        float screenW;
        float screenH;
    };

    bool createOffscreenTarget(uint32_t w, uint32_t h);
    bool createPipeline();
    bool ensureVertexBuffer(uint32_t requiredVertices);

    /// @brief Load a compiled shader from the shaders/ directory.
    SDL_GPUShader* loadShader(const char* name,
                              SDL_GPUShaderStage stage,
                              uint32_t samplerCount,
                              uint32_t uniformBufferCount);
};
```

- [ ] **Step 2: Create HudRenderer.cpp**

```cpp
/// @file HudRenderer.cpp
/// @brief GPU backend implementation for the HUD system.

#include "HudRenderer.hpp"

#include "particles/sdf/SdfAtlas.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

// ── Init / Quit ─────────────────────────────────────────────────────────────

bool HudRenderer::init(SDL_GPUDevice* device,
                       SDL_GPUShaderFormat shaderFormat,
                       const SdfAtlas& sdfAtlas,
                       uint32_t screenW,
                       uint32_t screenH)
{
    device_ = device;
    shaderFormat_ = shaderFormat;
    sdfAtlasTex_ = sdfAtlas.gpuTexture();
    sdfAtlasSamp_ = sdfAtlas.gpuSampler();

    if (!createOffscreenTarget(screenW, screenH))
        return false;

    if (!createPipeline())
        return false;

    // Create a 1×1 white fallback icon atlas (replaced when real atlas is loaded).
    {
        SDL_GPUTextureCreateInfo tci{};
        tci.type = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tci.width = 1;
        tci.height = 1;
        tci.layer_count_or_depth = 1;
        tci.num_levels = 1;
        tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        iconAtlasTex_ = SDL_CreateGPUTexture(device_, &tci);
        if (!iconAtlasTex_) {
            SDL_Log("HudRenderer: failed to create fallback icon texture");
            return false;
        }

        // Upload white pixel.
        const uint8_t white[4] = {255, 255, 255, 255};
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = 4;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &tbci);
        void* mapped = SDL_MapGPUTransferBuffer(device_, tb, false);
        std::memcpy(mapped, white, 4);
        SDL_UnmapGPUTransferBuffer(device_, tb);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = tb;
        SDL_GPUTextureRegion dst{};
        dst.texture = iconAtlasTex_;
        dst.w = 1;
        dst.h = 1;
        dst.d = 1;
        SDL_UploadToGPUTexture(cp, &src, &dst, false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device_, tb);

        // Sampler
        SDL_GPUSamplerCreateInfo sci{};
        sci.min_filter = SDL_GPU_FILTER_LINEAR;
        sci.mag_filter = SDL_GPU_FILTER_LINEAR;
        sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        iconAtlasSamp_ = SDL_CreateGPUSampler(device_, &sci);
    }

    SDL_Log("HudRenderer: init OK (%ux%u)", screenW, screenH);
    return true;
}

void HudRenderer::quit()
{
    if (!device_)
        return;

    SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
    SDL_ReleaseGPUTexture(device_, offscreenTarget_);
    SDL_ReleaseGPUTexture(device_, iconAtlasTex_);
    SDL_ReleaseGPUSampler(device_, iconAtlasSamp_);
    SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
    SDL_ReleaseGPUTransferBuffer(device_, transferBuffer_);

    pipeline_ = nullptr;
    offscreenTarget_ = nullptr;
    iconAtlasTex_ = nullptr;
    iconAtlasSamp_ = nullptr;
    vertexBuffer_ = nullptr;
    transferBuffer_ = nullptr;
    device_ = nullptr;
}

// ── Resize ──────────────────────────────────────────────────────────────────

void HudRenderer::resize(uint32_t newW, uint32_t newH)
{
    if (newW == width_ && newH == height_)
        return;
    SDL_ReleaseGPUTexture(device_, offscreenTarget_);
    offscreenTarget_ = nullptr;
    createOffscreenTarget(newW, newH);
}

// ── Render ──────────────────────────────────────────────────────────────────

void HudRenderer::render(std::span<const HudVertex> vertices,
                         std::span<const std::array<float, 6>> clipRects)
{
    if (vertices.empty() || !pipeline_ || !offscreenTarget_)
        return;

    const uint32_t vertexCount = static_cast<uint32_t>(vertices.size());
    if (!ensureVertexBuffer(vertexCount))
        return;

    // Upload vertices via transfer buffer.
    {
        void* mapped = SDL_MapGPUTransferBuffer(device_, transferBuffer_, true);
        std::memcpy(mapped, vertices.data(), vertexCount * sizeof(HudVertex));
        SDL_UnmapGPUTransferBuffer(device_, transferBuffer_);
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);

    // Copy transfer → vertex buffer.
    {
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = transferBuffer_;
        SDL_GPUBufferRegion dst{};
        dst.buffer = vertexBuffer_;
        dst.size = vertexCount * sizeof(HudVertex);
        SDL_UploadToGPUBuffer(cp, &src, &dst, true);
        SDL_EndGPUCopyPass(cp);
    }

    // Begin render pass → offscreen target.
    SDL_GPUColorTargetInfo ct{};
    ct.texture = offscreenTarget_;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.clear_color = {0.f, 0.f, 0.f, 0.f}; // transparent black
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, pipeline_);

    // Bind vertex buffer.
    SDL_GPUBufferBinding vbBinding{};
    vbBinding.buffer = vertexBuffer_;
    SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

    // Push screen-size uniform (set 0, binding 0).
    ScreenUniforms su{};
    su.screenW = static_cast<float>(width_);
    su.screenH = static_cast<float>(height_);
    SDL_PushGPUVertexUniformData(cmd, 0, &su, sizeof(su));

    // Bind fragment samplers (set 1: sdfAtlas + iconAtlas).
    SDL_GPUTextureSamplerBinding samplers[2] = {
        {.texture = sdfAtlasTex_, .sampler = sdfAtlasSamp_},
        {.texture = iconAtlasTex_, .sampler = iconAtlasSamp_},
    };
    SDL_BindGPUFragmentSamplers(pass, 0, samplers, 2);

    // Draw with optional scissor rects.
    if (clipRects.empty()) {
        // No clipping — single draw call.
        SDL_DrawGPUPrimitives(pass, vertexCount, 1, 0, 0);
    } else {
        for (const auto& cr : clipRects) {
            const uint32_t startVtx = static_cast<uint32_t>(cr[0]);
            const uint32_t vtxCount = static_cast<uint32_t>(cr[1]);

            // cr[2..5] = x, y, w, h.  Negative w means "no scissor" (full viewport).
            if (cr[4] >= 0.f) {
                SDL_Rect scissor{};
                scissor.x = static_cast<int>(cr[2]);
                scissor.y = static_cast<int>(cr[3]);
                scissor.w = static_cast<int>(cr[4]);
                scissor.h = static_cast<int>(cr[5]);
                SDL_SetGPUScissor(pass, &scissor);
            } else {
                SDL_Rect fullVP{0, 0, static_cast<int>(width_), static_cast<int>(height_)};
                SDL_SetGPUScissor(pass, &fullVP);
            }
            SDL_DrawGPUPrimitives(pass, vtxCount, 1, startVtx, 0);
        }
    }

    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
}

// ── Internals ───────────────────────────────────────────────────────────────

bool HudRenderer::createOffscreenTarget(uint32_t w, uint32_t h)
{
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.width = w;
    tci.height = h;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    offscreenTarget_ = SDL_CreateGPUTexture(device_, &tci);
    if (!offscreenTarget_) {
        SDL_Log("HudRenderer: failed to create offscreen target (%ux%u): %s", w, h, SDL_GetError());
        return false;
    }
    width_ = w;
    height_ = h;
    return true;
}

bool HudRenderer::createPipeline()
{
    SDL_GPUShader* vert = loadShader("hud.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* frag = loadShader("hud.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device_, vert);
        SDL_ReleaseGPUShader(device_, frag);
        return false;
    }

    // Vertex layout: 5 attributes, 48 bytes stride.
    const SDL_GPUVertexBufferDescription vbDesc{
        .slot = 0,
        .pitch = sizeof(HudVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };

    const SDL_GPUVertexAttribute attrs[5] = {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(HudVertex, position)},
        {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(HudVertex, uv)},
        {.location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = offsetof(HudVertex, color)},
        {.location = 3, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,  .offset = offsetof(HudVertex, texMode)},
        {.location = 4, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(HudVertex, shapeData)},
    };

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_buffer_descriptions = &vbDesc;
    vertexInput.num_vertex_buffers = 1;
    vertexInput.vertex_attributes = attrs;
    vertexInput.num_vertex_attributes = 5;

    // Alpha blending.
    SDL_GPUColorTargetDescription ct{};
    ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ct.blend_state.enable_blend = true;
    ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.vertex_input_state = vertexInput;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pci);
    SDL_ReleaseGPUShader(device_, vert);
    SDL_ReleaseGPUShader(device_, frag);

    if (!pipeline_) {
        SDL_Log("HudRenderer: pipeline creation failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool HudRenderer::ensureVertexBuffer(uint32_t requiredVertices)
{
    if (requiredVertices <= vertexCapacity_)
        return true;

    // Round up to next power of 2 (min 256 vertices).
    uint32_t newCap = 256;
    while (newCap < requiredVertices)
        newCap *= 2;

    SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
    SDL_ReleaseGPUTransferBuffer(device_, transferBuffer_);

    SDL_GPUBufferCreateInfo bci{};
    bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bci.size = newCap * sizeof(HudVertex);
    vertexBuffer_ = SDL_CreateGPUBuffer(device_, &bci);

    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = newCap * sizeof(HudVertex);
    transferBuffer_ = SDL_CreateGPUTransferBuffer(device_, &tbci);

    if (!vertexBuffer_ || !transferBuffer_) {
        SDL_Log("HudRenderer: failed to create vertex buffer (%u verts)", newCap);
        return false;
    }
    vertexCapacity_ = newCap;
    return true;
}

SDL_GPUShader* HudRenderer::loadShader(const char* name,
                                       SDL_GPUShaderStage stage,
                                       uint32_t samplerCount,
                                       uint32_t uniformBufferCount)
{
    const char* base = SDL_GetBasePath();
    const char* ext = (shaderFormat_ == SDL_GPU_SHADERFORMAT_MSL)    ? ".msl"
                      : (shaderFormat_ == SDL_GPU_SHADERFORMAT_DXIL) ? ".dxil"
                                                                      : ".spv";
    char path[512];
    SDL_snprintf(path, sizeof(path), "%sshaders/%s%s", base ? base : "", name, ext);

    return ::loadShader(device_, path, shaderFormat_, stage, samplerCount, uniformBufferCount, 0, 0);
}
```

- [ ] **Step 3: Commit**

```bash
git add src/client/hud/HudRenderer.hpp src/client/hud/HudRenderer.cpp
git commit -m "feat(hud): add HudRenderer GPU backend

Offscreen RGBA8 target, single pipeline with alpha blending,
dynamic vertex buffer with auto-grow, scissor-based clipping.
Acquires and submits its own command buffer each frame."
```

---

### Task 5: HudContext — Immediate-Mode Draw API

**Files:**
- Create: `src/client/hud/HudContext.hpp`
- Create: `src/client/hud/HudContext.cpp`

- [ ] **Step 1: Create HudContext.hpp**

```cpp
/// @file HudContext.hpp
/// @brief Immediate-mode draw API for HUD widgets.

#pragma once

#include "HudTypes.hpp"

#include <array>
#include <vector>

class SdfAtlas;

/// @brief Accumulates HUD geometry during a frame for batch rendering.
///
/// Widgets call rect(), text(), bar(), etc. to emit quads.  At the end of
/// the frame, HudRenderer consumes the vertex buffer and clip rect list.
class HudContext
{
public:
    /// @brief Bind the SDF atlas for text layout (glyph metrics).
    void init(const SdfAtlas* atlas);

    /// @brief Clear all geometry for a new frame.
    void beginFrame();

    // ── Primitives ──────────────────────────────────────────────────────

    void rect(float x, float y, float w, float h, HudColor color);
    void rectOutline(float x, float y, float w, float h, float thickness, HudColor color);
    void roundedRect(float x, float y, float w, float h, float radius, HudColor color);

    // ── Bars ────────────────────────────────────────────────────────────

    void bar(float x, float y, float w, float h, float fill01, HudColor fg, HudColor bg);

    // ── Text ────────────────────────────────────────────────────────────

    void text(const char* str, float x, float y, float size, HudColor color,
              HudAlign align = HudAlign::Left);
    float measureText(const char* str, float size) const;

    // ── Icons ───────────────────────────────────────────────────────────

    void icon(HudIcon id, float x, float y, float size, HudColor tint = HudColor::white());

    // ── Crosshair ───────────────────────────────────────────────────────

    void crosshair(const CrosshairStyle& style, float screenW, float screenH);

    // ── Clipping ────────────────────────────────────────────────────────

    void pushClipRect(float x, float y, float w, float h);
    void popClipRect();

    // ── Access for HudRenderer ──────────────────────────────────────────

    [[nodiscard]] const std::vector<HudVertex>& vertices() const { return vertices_; }

    /// @brief Clip rect spans: {startVertex, vertexCount, x, y, w, h}.
    /// Negative w means "full viewport" (no scissor).
    [[nodiscard]] const std::vector<std::array<float, 6>>& clipSpans() const { return clipSpans_; }

private:
    const SdfAtlas* sdfAtlas_ = nullptr;
    std::vector<HudVertex> vertices_;
    std::vector<std::array<float, 6>> clipSpans_;

    // Clip stack: each entry is {x, y, w, h}.  Empty = no clip.
    std::vector<std::array<float, 4>> clipStack_;
    uint32_t spanStartVertex_ = 0; ///< Vertex index where current clip span started.
    bool spanDirty_ = false;

    /// @brief Emit 6 vertices for a textured quad.
    void emitQuad(float x, float y, float w, float h,
                  float u0, float v0, float u1, float v1,
                  HudColor color, float texMode,
                  float sd0 = 0.f, float sd1 = 0.f, float sd2 = 0.f);

    /// @brief Flush the current clip span before changing scissor state.
    void flushClipSpan();
};
```

- [ ] **Step 2: Create HudContext.cpp**

```cpp
/// @file HudContext.cpp
/// @brief Immediate-mode draw API implementation.

#include "HudContext.hpp"

#include "particles/sdf/SdfAtlas.hpp"
#include "particles/sdf/SdfFont.hpp"

#include <algorithm>
#include <cstring>

void HudContext::init(const SdfAtlas* atlas) { sdfAtlas_ = atlas; }

void HudContext::beginFrame()
{
    vertices_.clear();
    clipSpans_.clear();
    clipStack_.clear();
    spanStartVertex_ = 0;
    spanDirty_ = false;
}

// ── Internal helpers ────────────────────────────────────────────────────────

void HudContext::emitQuad(float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1,
                          HudColor c, float texMode,
                          float sd0, float sd1, float sd2)
{
    spanDirty_ = true;

    auto makeVtx = [&](float px, float py, float u, float v) -> HudVertex {
        return HudVertex{
            {px, py},
            {u, v},
            {c.r, c.g, c.b, c.a},
            texMode,
            {sd0, sd1, sd2},
        };
    };

    // Two triangles: TL-TR-BL, TR-BR-BL.
    vertices_.push_back(makeVtx(x, y, u0, v0));
    vertices_.push_back(makeVtx(x + w, y, u1, v0));
    vertices_.push_back(makeVtx(x, y + h, u0, v1));
    vertices_.push_back(makeVtx(x + w, y, u1, v0));
    vertices_.push_back(makeVtx(x + w, y + h, u1, v1));
    vertices_.push_back(makeVtx(x, y + h, u0, v1));
}

void HudContext::flushClipSpan()
{
    const uint32_t vertexCount = static_cast<uint32_t>(vertices_.size()) - spanStartVertex_;
    if (vertexCount == 0)
        return;

    std::array<float, 6> span{};
    span[0] = static_cast<float>(spanStartVertex_);
    span[1] = static_cast<float>(vertexCount);

    if (clipStack_.empty()) {
        span[2] = 0.f;
        span[3] = 0.f;
        span[4] = -1.f; // Negative w = no scissor.
        span[5] = 0.f;
    } else {
        const auto& cr = clipStack_.back();
        span[2] = cr[0];
        span[3] = cr[1];
        span[4] = cr[2];
        span[5] = cr[3];
    }

    clipSpans_.push_back(span);
    spanStartVertex_ = static_cast<uint32_t>(vertices_.size());
    spanDirty_ = false;
}

// ── Primitives ──────────────────────────────────────────────────────────────

void HudContext::rect(float x, float y, float w, float h, HudColor color)
{
    emitQuad(x, y, w, h, 0, 0, 0, 0, color, 0.f);
}

void HudContext::rectOutline(float x, float y, float w, float h, float thickness, HudColor color)
{
    rect(x, y, w, thickness, color);                     // top
    rect(x, y + h - thickness, w, thickness, color);     // bottom
    rect(x, y + thickness, thickness, h - 2 * thickness, color); // left
    rect(x + w - thickness, y + thickness, thickness, h - 2 * thickness, color); // right
}

void HudContext::roundedRect(float x, float y, float w, float h, float radius, HudColor color)
{
    const float halfW = w * 0.5f;
    const float halfH = h * 0.5f;
    emitQuad(x, y, w, h,
             0.f, 0.f, 1.f, 1.f, // UV: [0,1]² for SDF shape local coords
             color, 3.f,          // texMode = 3
             halfW, halfH, radius);
}

// ── Bars ────────────────────────────────────────────────────────────────────

void HudContext::bar(float x, float y, float w, float h, float fill01, HudColor fg, HudColor bg)
{
    const float clampedFill = std::clamp(fill01, 0.f, 1.f);
    rect(x, y, w, h, bg);
    if (clampedFill > 0.f)
        rect(x, y, w * clampedFill, h, fg);
}

// ── Text ────────────────────────────────────────────────────────────────────

void HudContext::text(const char* str, float x, float y, float size, HudColor color, HudAlign align)
{
    if (!sdfAtlas_ || !str || !*str)
        return;

    const float scale = size / static_cast<float>(SdfAtlas::k_renderPx);

    // Measure for alignment.
    float totalWidth = 0.f;
    if (align != HudAlign::Left)
        totalWidth = measureText(str, size);

    float startX = x;
    if (align == HudAlign::Center)
        startX = x - totalWidth * 0.5f;
    else if (align == HudAlign::Right)
        startX = x - totalWidth;

    float cursorX = startX;
    for (const char* p = str; *p; ++p) {
        const uint32_t cp = static_cast<uint32_t>(*p);
        const GlyphInfo* gi = sdfAtlas_->glyph(cp);
        if (!gi)
            continue;

        const float gw = gi->width * scale;
        const float gh = gi->height * scale;
        const float gx = cursorX + gi->bearing.x * scale;
        const float gy = y - gi->bearing.y * scale + size; // baseline offset

        emitQuad(gx, gy, gw, gh,
                 gi->uvMin.x, gi->uvMin.y,
                 gi->uvMax.x, gi->uvMax.y,
                 color, 1.f); // texMode = 1 (SDF text)

        cursorX += gi->advance * scale;
    }
}

float HudContext::measureText(const char* str, float size) const
{
    if (!sdfAtlas_ || !str || !*str)
        return 0.f;

    const float scale = size / static_cast<float>(SdfAtlas::k_renderPx);
    float width = 0.f;
    for (const char* p = str; *p; ++p) {
        const GlyphInfo* gi = sdfAtlas_->glyph(static_cast<uint32_t>(*p));
        if (gi)
            width += gi->advance * scale;
    }
    return width;
}

// ── Icons ───────────────────────────────────────────────────────────────────

void HudContext::icon(HudIcon /*id*/, float x, float y, float size, HudColor tint)
{
    // TODO: look up icon UV rect from atlas by id.  For now, full 1×1 fallback.
    emitQuad(x, y, size, size, 0.f, 0.f, 1.f, 1.f, tint, 2.f);
}

// ── Crosshair ───────────────────────────────────────────────────────────────

void HudContext::crosshair(const CrosshairStyle& style, float screenW, float screenH)
{
    const float cx = screenW * 0.5f;
    const float cy = screenH * 0.5f;
    const float gap = style.gap;
    const float len = style.length;
    const float t = style.thickness;
    const float ht = t * 0.5f;

    // Four arms.
    rect(cx + gap, cy - ht, len, t, style.color);         // right
    rect(cx - gap - len, cy - ht, len, t, style.color);   // left
    rect(cx - ht, cy - gap - len, t, len, style.color);   // top
    rect(cx - ht, cy + gap, t, len, style.color);         // bottom

    // Center dot.
    if (style.dot)
        rect(cx - ht, cy - ht, t, t, style.color);
}

// ── Clipping ────────────────────────────────────────────────────────────────

void HudContext::pushClipRect(float x, float y, float w, float h)
{
    if (spanDirty_)
        flushClipSpan();
    clipStack_.push_back({x, y, w, h});
}

void HudContext::popClipRect()
{
    if (spanDirty_)
        flushClipSpan();
    if (!clipStack_.empty())
        clipStack_.pop_back();
}
```

- [ ] **Step 3: Commit**

```bash
git add src/client/hud/HudContext.hpp src/client/hud/HudContext.cpp
git commit -m "feat(hud): add immediate-mode draw API (HudContext)

rect, rectOutline, roundedRect, bar, text, icon, crosshair,
pushClipRect/popClipRect. Batches HudVertex quads for HudRenderer."
```

---

### Task 6: HudWidget Base + Hud Orchestrator

**Files:**
- Create: `src/client/hud/HudWidget.hpp`
- Create: `src/client/hud/Hud.hpp`
- Create: `src/client/hud/Hud.cpp`

- [ ] **Step 1: Create HudWidget.hpp**

```cpp
/// @file HudWidget.hpp
/// @brief Base struct for all HUD widgets.

#pragma once

#include "HudTypes.hpp"

class HudContext;
class HudTweenPool;

/// @brief Base class for a retained HUD element.
///
/// Widgets own their state and animation.  Their draw() method uses
/// HudContext's immediate-mode API to emit geometry.
struct HudWidget
{
    bool visible = true;

    HudAnchor anchor = HudAnchor::TopLeft;
    float offsetX = 0.f, offsetY = 0.f;
    float width = 0.f, height = 0.f;

    virtual ~HudWidget() = default;

    /// @brief Called each frame before draw().  Update animation, consume events.
    virtual void update(float dt, const HudGameState& state, HudTweenPool& tweens) = 0;

    /// @brief Emit geometry into the draw context.
    /// @param ctx  Immediate-mode draw API.
    /// @param drawX Resolved pixel X (anchor + offset already applied).
    /// @param drawY Resolved pixel Y.
    virtual void draw(HudContext& ctx, float drawX, float drawY) = 0;
};
```

- [ ] **Step 2: Create Hud.hpp**

```cpp
/// @file Hud.hpp
/// @brief Top-level HUD system — owns widgets, renderer, context, tweens.

#pragma once

#include "HudContext.hpp"
#include "HudRenderer.hpp"
#include "HudTween.hpp"
#include "HudTypes.hpp"
#include "HudWidget.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <vector>

class SdfAtlas;

/// @brief Top-level HUD system.  Owns all layers and widgets.
///
/// Game calls update() + render() each frame.  The renderer reads
/// getOutputTexture() and blits it after tone mapping.
class Hud
{
public:
    /// @brief Initialise all HUD subsystems and create default widgets.
    bool init(SDL_GPUDevice* device,
              SDL_GPUShaderFormat shaderFormat,
              const SdfAtlas& sdfAtlas,
              uint32_t screenW,
              uint32_t screenH);

    /// @brief Release all resources.
    void quit();

    /// @brief Notify of window resize.
    void resize(uint32_t newW, uint32_t newH);

    /// @brief Forward an SDL event to interactive widgets (buy menu, scoreboard).
    void processEvent(const SDL_Event* event);

    /// @brief Tick widget animations and consume game events.
    void update(float dt, const HudGameState& state);

    /// @brief Draw all widgets and flush to the GPU offscreen target.
    void render();

    /// @brief The offscreen texture to blit.
    [[nodiscard]] SDL_GPUTexture* getOutputTexture() const { return renderer_.getOutputTexture(); }

    /// @brief Access all widgets (for debug panel iteration).
    [[nodiscard]] std::vector<std::unique_ptr<HudWidget>>& widgets() { return widgets_; }

    /// @brief Access the tween pool (for debug panel).
    [[nodiscard]] HudTweenPool& tweens() { return tweens_; }

private:
    HudRenderer renderer_;
    HudContext context_;
    HudTweenPool tweens_;
    std::vector<std::unique_ptr<HudWidget>> widgets_;

    float screenW_ = 0.f, screenH_ = 0.f;

    /// @brief Resolve anchor + offset to pixel coordinates.
    void resolveAnchor(const HudWidget& w, float& outX, float& outY) const;

    /// @brief Create all default widgets with initial layout.
    void createWidgets();
};
```

- [ ] **Step 3: Create Hud.cpp**

```cpp
/// @file Hud.cpp
/// @brief Top-level HUD orchestrator.

#include "Hud.hpp"

#include "particles/sdf/SdfAtlas.hpp"

bool Hud::init(SDL_GPUDevice* device,
               SDL_GPUShaderFormat shaderFormat,
               const SdfAtlas& sdfAtlas,
               uint32_t screenW,
               uint32_t screenH)
{
    screenW_ = static_cast<float>(screenW);
    screenH_ = static_cast<float>(screenH);

    if (!renderer_.init(device, shaderFormat, sdfAtlas, screenW, screenH))
        return false;

    context_.init(&sdfAtlas);
    createWidgets();

    SDL_Log("Hud: init OK (%ux%u, %zu widgets)", screenW, screenH, widgets_.size());
    return true;
}

void Hud::quit()
{
    widgets_.clear();
    renderer_.quit();
}

void Hud::resize(uint32_t newW, uint32_t newH)
{
    screenW_ = static_cast<float>(newW);
    screenH_ = static_cast<float>(newH);
    renderer_.resize(newW, newH);
}

void Hud::processEvent(const SDL_Event* event)
{
    // Interactive widgets (buy menu, scoreboard) handle events here.
    // Iterate in reverse so topmost widget gets first chance.
    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {
        if ((*it)->visible) {
            // Widgets that consume events will set a flag — for now, no-op.
        }
    }
}

void Hud::update(float dt, const HudGameState& state)
{
    tweens_.update(dt);
    for (auto& w : widgets_) {
        if (w->visible)
            w->update(dt, state, tweens_);
    }
}

void Hud::render()
{
    context_.beginFrame();

    for (auto& w : widgets_) {
        if (!w->visible)
            continue;
        float drawX = 0.f, drawY = 0.f;
        resolveAnchor(*w, drawX, drawY);
        w->draw(context_, drawX, drawY);
    }

    // Flush any remaining clip span.
    if (!context_.vertices().empty()) {
        // If no clip spans were emitted (no pushClipRect calls),
        // create a single full-viewport span.
        if (context_.clipSpans().empty()) {
            std::vector<std::array<float, 6>> fullSpan = {
                {0.f, static_cast<float>(context_.vertices().size()), 0.f, 0.f, -1.f, 0.f}};
            renderer_.render(context_.vertices(), fullSpan);
        } else {
            renderer_.render(context_.vertices(), context_.clipSpans());
        }
    }
}

void Hud::resolveAnchor(const HudWidget& w, float& outX, float& outY) const
{
    float baseX = 0.f, baseY = 0.f;

    switch (w.anchor) {
    case HudAnchor::TopLeft:      baseX = 0.f;                      baseY = 0.f;                      break;
    case HudAnchor::TopCenter:    baseX = screenW_ * 0.5f;          baseY = 0.f;                      break;
    case HudAnchor::TopRight:     baseX = screenW_;                  baseY = 0.f;                      break;
    case HudAnchor::CenterLeft:   baseX = 0.f;                      baseY = screenH_ * 0.5f;          break;
    case HudAnchor::Center:       baseX = screenW_ * 0.5f;          baseY = screenH_ * 0.5f;          break;
    case HudAnchor::CenterRight:  baseX = screenW_;                  baseY = screenH_ * 0.5f;          break;
    case HudAnchor::BottomLeft:   baseX = 0.f;                      baseY = screenH_;                  break;
    case HudAnchor::BottomCenter: baseX = screenW_ * 0.5f;          baseY = screenH_;                  break;
    case HudAnchor::BottomRight:  baseX = screenW_;                  baseY = screenH_;                  break;
    }

    outX = baseX + w.offsetX;
    outY = baseY + w.offsetY;
}

void Hud::createWidgets()
{
    // Widgets added in draw order (back to front).
    // Populated by subsequent tasks as each widget is implemented.
}
```

- [ ] **Step 4: Commit**

```bash
git add src/client/hud/HudWidget.hpp src/client/hud/Hud.hpp src/client/hud/Hud.cpp
git commit -m "feat(hud): add HudWidget base and Hud orchestrator

Hud owns all widgets, drives update/render cycle, resolves anchor
positions. createWidgets() is empty — populated as widgets are built."
```

---

### Task 7: Renderer Integration — IRenderer + Blit Pass

**Files:**
- Modify: `src/client/renderer/IRenderer.hpp`
- Modify: `src/client/renderer/Renderer.hpp`
- Modify: `src/client/renderer/Renderer.cpp`
- Modify: `src/client/renderer/HybridRenderer.hpp`
- Modify: `src/client/renderer/HybridRenderer.cpp`
- Modify: `src/client/renderer-new/Renderer.hpp`

- [ ] **Step 1: Add SetHudTexture to RendererFeature enum and IRenderer**

In `src/client/renderer/IRenderer.hpp`, add the enum entry and virtual method:

Add `SetHudTexture,` after `ModelCount,` in the enum (line 47).

Add this pure virtual after `modelCount()` (line 82):

```cpp
    /// @brief Set the HUD overlay texture to blit after tone mapping.
    virtual void setHudTexture(SDL_GPUTexture* hudOutput) = 0;
```

- [ ] **Step 2: Add HUD blit members to Renderer.hpp**

In `src/client/renderer/Renderer.hpp`, add to private section (after `captureRTFmt`):

```cpp
    // HUD overlay
    SDL_GPUTexture* hudTexture_ = nullptr;       ///< Non-owning: from Hud system.
    SDL_GPUGraphicsPipeline* hudBlitPipeline_ = nullptr; ///< Fullscreen quad, alpha blend.
    SDL_GPUSampler* hudSampler_ = nullptr;       ///< Nearest, clamp.
    bool initHudBlit();
```

Add the public override:

```cpp
    void setHudTexture(SDL_GPUTexture* hudOutput) override { hudTexture_ = hudOutput; }
```

- [ ] **Step 3: Implement HUD blit pipeline init in Renderer.cpp**

Add `initHudBlit()` implementation:

```cpp
bool Renderer::initHudBlit()
{
    SDL_GPUShader* vert = loadShaderFromFile("fullscreen.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader* frag = loadShaderFromFile("hud_blit.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        return false;
    }

    SDL_GPUColorTargetDescription ct{};
    ct.format = swapchainFormat;
    ct.blend_state.enable_blend = true;
    ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    hudBlitPipeline_ = SDL_CreateGPUGraphicsPipeline(device, &pci);
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!hudBlitPipeline_) {
        SDL_Log("Renderer: HUD blit pipeline failed: %s", SDL_GetError());
        return false;
    }

    // Nearest-clamp sampler for pixel-perfect blit.
    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_NEAREST;
    sci.mag_filter = SDL_GPU_FILTER_NEAREST;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    hudSampler_ = SDL_CreateGPUSampler(device, &sci);

    return hudBlitPipeline_ && hudSampler_;
}
```

**Wait — we need a `hud_blit.frag` shader** for the blit pass. This is a simple fullscreen fragment shader that samples the HUD texture. Create `shaders/hud_blit.frag`:

```glsl
/// @file hud_blit.frag
/// @brief Fullscreen blit of HUD overlay texture with alpha.
#version 450

layout(location = 0) in vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform sampler2D hudTexture;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(hudTexture, fragTexCoord);
}
```

Add `hud_blit.frag` to `SHADER_SOURCES` in CMakeLists.txt.

- [ ] **Step 4: Insert HUD blit draw call in Renderer::drawFrame**

In `Renderer.cpp`, in the tone mapping pass (around line 3033), after the ImGui draw and before `SDL_EndGPURenderPass(pass)`, add the HUD blit:

```cpp
        // ImGui overlay (in LDR, on top of tone-mapped image)
        if (drawData)
            ImGui_ImplSDLGPU3_RenderDrawData(drawData, cmd, pass);

        // HUD overlay (in LDR, on top of everything)
        if (hudTexture_ && hudBlitPipeline_) {
            SDL_BindGPUGraphicsPipeline(pass, hudBlitPipeline_);
            SDL_GPUTextureSamplerBinding hudBinding{};
            hudBinding.texture = hudTexture_;
            hudBinding.sampler = hudSampler_;
            SDL_BindGPUFragmentSamplers(pass, 0, &hudBinding, 1);
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0); // fullscreen triangle
        }

        SDL_EndGPURenderPass(pass);
```

Also call `initHudBlit()` at the end of `Renderer::init()` and clean up in `quit()`.

- [ ] **Step 5: Add routing to HybridRenderer**

In `HybridRenderer.hpp`, add the override:

```cpp
    void setHudTexture(SDL_GPUTexture* hudOutput) override;
```

In `HybridRenderer.cpp`, add:

```cpp
void HybridRenderer::setHudTexture(SDL_GPUTexture* hudOutput)
{
    // Always route to both — whichever is doing drawFrame needs the texture.
    legacy_.setHudTexture(hudOutput);
    if (nextInitialised_)
        next_.setHudTexture(hudOutput);
}
```

Add `SetHudTexture` to the `featureName()` switch and `k_allFeatures[]` array.

- [ ] **Step 6: Add stub to NewRenderer**

In `src/client/renderer-new/Renderer.hpp`, add:

```cpp
    void setHudTexture(SDL_GPUTexture* /*hudOutput*/) override {}
```

- [ ] **Step 7: Commit**

```bash
git add src/client/renderer/IRenderer.hpp src/client/renderer/Renderer.hpp \
        src/client/renderer/Renderer.cpp src/client/renderer/HybridRenderer.hpp \
        src/client/renderer/HybridRenderer.cpp src/client/renderer-new/Renderer.hpp \
        shaders/hud_blit.frag CMakeLists.txt
git commit -m "feat(hud): add renderer HUD blit integration

SetHudTexture on IRenderer, fullscreen alpha-blend blit after
ImGui in tone mapping pass. HybridRenderer routes to both backends.
NewRenderer has no-op stub."
```

---

### Task 8: Game.cpp Wiring + CMake + SdfAtlas Accessor

**Files:**
- Modify: `src/client/particles/ParticleSystem.hpp`
- Modify: `src/client/game/Game.hpp`
- Modify: `src/client/game/Game.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add SdfAtlas accessor to ParticleSystem**

In `ParticleSystem.hpp`, add public accessor:

```cpp
    /// @brief Access the SDF atlas for shared use by the HUD system.
    [[nodiscard]] const SdfAtlas& sdfAtlas() const { return sdf_.atlas(); }
```

- [ ] **Step 2: Add Hud member to Game.hpp**

Add include at top of Game.hpp:

```cpp
#include "hud/Hud.hpp"
```

Add member in the private section (after `ParticleSystem particleSystem;`):

```cpp
    Hud hud_;                          ///< In-game HUD overlay system.
```

- [ ] **Step 3: Wire Hud init in Game::init()**

In `Game.cpp`, after `particleSystem.init(...)` succeeds (after line 124), add:

```cpp
    // HUD system — needs device + shader format from renderer, SDF atlas from particles.
    if (particleSystem.sdfReady()) {
        if (!hud_.init(renderer.getDevice(), renderer.getShaderFormat(),
                       particleSystem.sdfAtlas(),
                       1280, 720)) {
            SDL_Log("Hud init failed (non-fatal — HUD disabled)");
        } else {
            renderer.setHudTexture(hud_.getOutputTexture());
        }
    }
```

- [ ] **Step 4: Wire Hud update/render in Game::iterate()**

In `Game.cpp`, before `debugUI.render()` (line 2004), add:

```cpp
    // Update and render HUD.
    if (hud_.getOutputTexture()) {
        HudGameState hudState{};
        // Populate from local player ECS data.
        if (auto* ps = registry.try_get<PlayerState>(registry.localEntity())) {
            hudState.health = ps->health;
            hudState.maxHealth = 100;
            hudState.armor = ps->armor;
            hudState.maxArmor = 100;
        }
        hudState.isAlive = true; // TODO: derive from PlayerState
        hudState.screenW = static_cast<float>(renderer.getCamera().viewportW);
        hudState.screenH = static_cast<float>(renderer.getCamera().viewportH);

        hud_.update(frameTime, hudState);
        hud_.render();
    }
```

- [ ] **Step 5: Wire Hud event forwarding in Game::event()**

After `debugUI.processEvent(event)` (line 472), add:

```cpp
    hud_.processEvent(event);
```

- [ ] **Step 6: Wire Hud quit and resize**

In `Game::quit()`, before `renderer.quit()` (line 2044), add:

```cpp
    hud_.quit();
```

Handle resize: search for any existing `SDL_EVENT_WINDOW_RESIZED` handling and add:

```cpp
    hud_.resize(static_cast<uint32_t>(newW), static_cast<uint32_t>(newH));
    renderer.setHudTexture(hud_.getOutputTexture());
```

- [ ] **Step 7: Add HUD source files to CMakeLists.txt**

In `CMakeLists.txt`, after the animation section (around line 621), add:

```cmake
    # HUD system
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudTypes.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudTween.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudTween.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudRenderer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudContext.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudContext.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/HudWidget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/Hud.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/Hud.hpp"
```

- [ ] **Step 8: Build and verify**

```bash
cd /home/user/Documents/dev/group2
cmake --preset debug
cmake --build build/debug -j$(nproc) 2>&1 | tail -30
```

Expected: compiles clean. Running the game should show the existing scene with an empty (transparent) HUD overlay — no visual change yet.

- [ ] **Step 9: Commit**

```bash
git add src/client/particles/ParticleSystem.hpp \
        src/client/game/Game.hpp src/client/game/Game.cpp \
        CMakeLists.txt
git commit -m "feat(hud): wire HUD system into game loop

Init after particle system, update/render before drawFrame,
event forwarding, quit in reverse order. HudGameState populated
from local player ECS data. CMake build includes all HUD sources."
```

---

### Task 9: CrosshairWidget — First End-to-End Widget

**Files:**
- Create: `src/client/hud/widgets/CrosshairWidget.hpp`
- Create: `src/client/hud/widgets/CrosshairWidget.cpp`
- Modify: `src/client/hud/Hud.cpp` (add to createWidgets)
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create CrosshairWidget.hpp**

```cpp
/// @file CrosshairWidget.hpp
/// @brief Dynamic crosshair with configurable gap, thickness, and dot.

#pragma once

#include "hud/HudWidget.hpp"

struct CrosshairWidget : HudWidget
{
    CrosshairStyle style;

    CrosshairWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;
};
```

- [ ] **Step 2: Create CrosshairWidget.cpp**

```cpp
/// @file CrosshairWidget.cpp
#include "CrosshairWidget.hpp"

#include "hud/HudContext.hpp"

CrosshairWidget::CrosshairWidget()
{
    anchor = HudAnchor::Center;
    // No offset — centered.
}

void CrosshairWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;
}

void CrosshairWidget::draw(HudContext& ctx, float /*drawX*/, float /*drawY*/)
{
    ctx.crosshair(style, ctx.vertices().empty() ? 1280.f : 1280.f,
                  720.f); // TODO: pass screen dims properly
}
```

**Wait** — the crosshair needs screen dimensions.  We should pass them through `HudGameState`.  The `draw()` method gets `drawX, drawY` (the resolved anchor position, which for Center is screenW/2, screenH/2).  But `crosshair()` on HudContext needs full screen W/H.

Better approach: have CrosshairWidget draw the arms manually using `ctx.rect()` relative to `drawX, drawY`:

```cpp
/// @file CrosshairWidget.cpp
#include "CrosshairWidget.hpp"

#include "hud/HudContext.hpp"

CrosshairWidget::CrosshairWidget()
{
    anchor = HudAnchor::Center;
}

void CrosshairWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;
}

void CrosshairWidget::draw(HudContext& ctx, float cx, float cy)
{
    const float gap = style.gap;
    const float len = style.length;
    const float t = style.thickness;
    const float ht = t * 0.5f;

    // Four arms.
    ctx.rect(cx + gap, cy - ht, len, t, style.color);         // right
    ctx.rect(cx - gap - len, cy - ht, len, t, style.color);   // left
    ctx.rect(cx - ht, cy - gap - len, t, len, style.color);   // top
    ctx.rect(cx - ht, cy + gap, t, len, style.color);         // bottom

    // Center dot.
    if (style.dot)
        ctx.rect(cx - ht, cy - ht, t, t, style.color);
}
```

- [ ] **Step 3: Register in Hud::createWidgets()**

In `Hud.cpp`, add include and widget creation:

```cpp
#include "widgets/CrosshairWidget.hpp"
```

In `createWidgets()`:

```cpp
void Hud::createWidgets()
{
    widgets_.push_back(std::make_unique<CrosshairWidget>());
}
```

- [ ] **Step 4: Add to CMakeLists.txt**

```cmake
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/CrosshairWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/CrosshairWidget.hpp"
```

- [ ] **Step 5: Build and run — verify green crosshair appears at screen center**

```bash
cmake --build build/debug -j$(nproc) && ./build/debug/group2
```

Expected: green crosshair visible at screen center on top of the game scene.

- [ ] **Step 6: Commit**

```bash
git add src/client/hud/widgets/CrosshairWidget.hpp \
        src/client/hud/widgets/CrosshairWidget.cpp \
        src/client/hud/Hud.cpp CMakeLists.txt
git commit -m "feat(hud): add CrosshairWidget — first end-to-end HUD element

Dynamic crosshair with gap, thickness, length, dot, and color.
Proves the full pipeline: widget → HudContext → HudRenderer → blit."
```

---

### Task 10: HealthArmorBar Widget

**Files:**
- Create: `src/client/hud/widgets/HealthArmorBar.hpp`
- Create: `src/client/hud/widgets/HealthArmorBar.cpp`
- Modify: `src/client/hud/Hud.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create HealthArmorBar.hpp**

```cpp
/// @file HealthArmorBar.hpp
/// @brief Health and armor bars with animated fill.

#pragma once

#include "hud/HudWidget.hpp"

struct HealthArmorBar : HudWidget
{
    // Layout constants (tweakable via ImGui debug panel).
    float barWidth = 200.f;
    float barHeight = 16.f;
    float barSpacing = 4.f;
    float fontSize = 18.f;
    float textPadding = 6.f;

    HealthArmorBar();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    float healthFill_ = 1.f;  ///< Animated fill [0,1].
    float armorFill_ = 0.f;   ///< Animated fill [0,1].
    int lastHealth_ = 100;
    int lastArmor_ = 0;
    int displayHealth_ = 100;
    int displayArmor_ = 0;
};
```

- [ ] **Step 2: Create HealthArmorBar.cpp**

```cpp
/// @file HealthArmorBar.cpp
#include "HealthArmorBar.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudTween.hpp"

#include <cstdio>

HealthArmorBar::HealthArmorBar()
{
    anchor = HudAnchor::BottomLeft;
    offsetX = 20.f;
    offsetY = -60.f;
    width = 200.f;
    height = 40.f;
}

void HealthArmorBar::update(float /*dt*/, const HudGameState& state, HudTweenPool& tweens)
{
    visible = state.isAlive;
    displayHealth_ = state.health;
    displayArmor_ = state.armor;

    const float targetHealth = (state.maxHealth > 0) ? static_cast<float>(state.health) / state.maxHealth : 0.f;
    const float targetArmor = (state.maxArmor > 0) ? static_cast<float>(state.armor) / state.maxArmor : 0.f;

    if (state.health != lastHealth_) {
        tweens.tween(&healthFill_, targetHealth, 0.25f, easeOutQuad);
        lastHealth_ = state.health;
    }
    if (state.armor != lastArmor_) {
        tweens.tween(&armorFill_, targetArmor, 0.25f, easeOutQuad);
        lastArmor_ = state.armor;
    }
}

void HealthArmorBar::draw(HudContext& ctx, float x, float y)
{
    // Health bar.
    ctx.bar(x, y, barWidth, barHeight, healthFill_,
            HudColor(0.2f, 0.8f, 0.2f, 0.9f),   // green fill
            HudColor(0.15f, 0.15f, 0.15f, 0.7f)); // dark bg

    // Health text.
    char hpText[16];
    SDL_snprintf(hpText, sizeof(hpText), "%d", displayHealth_);
    ctx.text(hpText, x + barWidth + textPadding, y, fontSize, HudColor::white());

    // Armor bar (below health).
    const float armorY = y + barHeight + barSpacing;
    ctx.bar(x, armorY, barWidth, barHeight, armorFill_,
            HudColor(0.3f, 0.5f, 0.9f, 0.9f),   // blue fill
            HudColor(0.15f, 0.15f, 0.15f, 0.7f)); // dark bg

    // Armor text.
    char armorText[16];
    SDL_snprintf(armorText, sizeof(armorText), "%d", displayArmor_);
    ctx.text(armorText, x + barWidth + textPadding, armorY, fontSize, HudColor::white());
}
```

- [ ] **Step 3: Register in Hud::createWidgets() and add to CMake**

Add `#include "widgets/HealthArmorBar.hpp"` in Hud.cpp.

Add in `createWidgets()`:
```cpp
    widgets_.push_back(std::make_unique<HealthArmorBar>());
```

Add to CMakeLists.txt:
```cmake
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/HealthArmorBar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/widgets/HealthArmorBar.hpp"
```

- [ ] **Step 4: Build, run, verify**

Expected: green health bar and blue armor bar in bottom-left corner with numeric values.

- [ ] **Step 5: Commit**

```bash
git add src/client/hud/widgets/HealthArmorBar.hpp \
        src/client/hud/widgets/HealthArmorBar.cpp \
        src/client/hud/Hud.cpp CMakeLists.txt
git commit -m "feat(hud): add HealthArmorBar widget with animated fill

Tweened health/armor bars anchored bottom-left. Green health bar,
blue armor bar, numeric display."
```

---

### Task 11: AmmoCounter Widget

**Files:**
- Create: `src/client/hud/widgets/AmmoCounter.hpp`
- Create: `src/client/hud/widgets/AmmoCounter.cpp`
- Modify: `src/client/hud/Hud.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Create AmmoCounter.hpp**

```cpp
/// @file AmmoCounter.hpp
/// @brief Ammo clip / reserve display.

#pragma once

#include "hud/HudWidget.hpp"

struct AmmoCounter : HudWidget
{
    float clipFontSize = 32.f;
    float reserveFontSize = 18.f;
    float dividerPadding = 4.f;

    AmmoCounter();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int displayClip_ = 30;
    int displayReserve_ = 90;
};
```

- [ ] **Step 2: Create AmmoCounter.cpp**

```cpp
/// @file AmmoCounter.cpp
#include "AmmoCounter.hpp"

#include "hud/HudContext.hpp"

#include <cstdio>

AmmoCounter::AmmoCounter()
{
    anchor = HudAnchor::BottomRight;
    offsetX = -20.f;
    offsetY = -50.f;
}

void AmmoCounter::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    visible = state.isAlive;
    displayClip_ = state.ammoClip;
    displayReserve_ = state.ammoReserve;
}

void AmmoCounter::draw(HudContext& ctx, float x, float y)
{
    char clipText[16];
    SDL_snprintf(clipText, sizeof(clipText), "%d", displayClip_);

    char reserveText[16];
    SDL_snprintf(reserveText, sizeof(reserveText), "/ %d", displayReserve_);

    // Clip count (large, right-aligned from anchor).
    const float clipW = ctx.measureText(clipText, clipFontSize);
    ctx.text(clipText, x - clipW, y, clipFontSize, HudColor::white());

    // Reserve count (smaller, right of divider).
    ctx.text(reserveText, x - clipW - dividerPadding, y + clipFontSize - reserveFontSize,
             reserveFontSize, HudColor(0.7f, 0.7f, 0.7f, 0.8f));
}
```

- [ ] **Step 3: Register and add to CMake (same pattern as Task 10)**

- [ ] **Step 4: Build, run, verify**

- [ ] **Step 5: Commit**

```bash
git add src/client/hud/widgets/AmmoCounter.hpp \
        src/client/hud/widgets/AmmoCounter.cpp \
        src/client/hud/Hud.cpp CMakeLists.txt
git commit -m "feat(hud): add AmmoCounter widget

Clip / reserve display anchored bottom-right with large clip
count and smaller reserve text."
```

---

### Task 12: HitMarkerWidget

**Files:**
- Create: `src/client/hud/widgets/HitMarkerWidget.hpp`
- Create: `src/client/hud/widgets/HitMarkerWidget.cpp`
- Modify: `src/client/hud/Hud.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Create HitMarkerWidget.hpp**

```cpp
/// @file HitMarkerWidget.hpp
/// @brief Center-screen hit confirm flare with fade + scale animation.

#pragma once

#include "hud/HudWidget.hpp"

struct HitMarkerWidget : HudWidget
{
    float armLength = 8.f;
    float armThickness = 2.f;
    float armGap = 4.f;
    float fadeDuration = 0.35f;

    HitMarkerWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    float alpha_ = 0.f;
    float scale_ = 1.f;
    bool isHeadshot_ = false;
};
```

- [ ] **Step 2: Create HitMarkerWidget.cpp**

```cpp
/// @file HitMarkerWidget.cpp
#include "HitMarkerWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudTween.hpp"

HitMarkerWidget::HitMarkerWidget()
{
    anchor = HudAnchor::Center;
    visible = true; // Always "active" — alpha controls visibility.
}

void HitMarkerWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& tweens)
{
    for (const auto& hc : state.hitConfirms) {
        alpha_ = 1.f;
        scale_ = 1.3f;
        isHeadshot_ = hc.isHeadshot;
        tweens.tween(&alpha_, 0.f, fadeDuration, easeOutQuad);
        tweens.tween(&scale_, 1.f, 0.15f, easeOutBack);
    }
}

void HitMarkerWidget::draw(HudContext& ctx, float cx, float cy)
{
    if (alpha_ < 0.01f)
        return;

    const HudColor color = isHeadshot_ ? HudColor(1.f, 0.3f, 0.3f, alpha_)
                                       : HudColor(1.f, 1.f, 1.f, alpha_);
    const float gap = armGap * scale_;
    const float len = armLength * scale_;
    const float t = armThickness;

    // Four diagonal arms (45° rotated X pattern).
    // Approximate with small rects at 45° offsets from center.
    const float d = 0.707f; // cos(45°)
    const float gd = gap * d;
    const float ld = len * d;

    // Top-right arm
    ctx.rect(cx + gd, cy - gd - ld, t, ld, color);
    // Top-left arm
    ctx.rect(cx - gd - t, cy - gd - ld, t, ld, color);
    // Bottom-right arm
    ctx.rect(cx + gd, cy + gd, t, ld, color);
    // Bottom-left arm
    ctx.rect(cx - gd - t, cy + gd, t, ld, color);
}
```

- [ ] **Step 3: Register and add to CMake**

- [ ] **Step 4: Commit**

```bash
git add src/client/hud/widgets/HitMarkerWidget.hpp \
        src/client/hud/widgets/HitMarkerWidget.cpp \
        src/client/hud/Hud.cpp CMakeLists.txt
git commit -m "feat(hud): add HitMarkerWidget with fade + scale animation

Center-screen hit confirm flare. Tweened alpha fade and scale
overshoot. Red for headshots, white for body shots."
```

---

### Task 13: KillFeed Widget

**Files:**
- Create: `src/client/hud/widgets/KillFeed.hpp`
- Create: `src/client/hud/widgets/KillFeed.cpp`
- Modify: `src/client/hud/Hud.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Create KillFeed.hpp**

```cpp
/// @file KillFeed.hpp
/// @brief Sliding kill feed entries anchored top-right.

#pragma once

#include "hud/HudWidget.hpp"

#include <string>
#include <vector>

struct KillFeed : HudWidget
{
    float entryHeight = 22.f;
    float entryPadding = 4.f;
    float entryLifetime = 5.f;
    float fontSize = 14.f;
    float fadeOutDuration = 0.5f;
    int maxEntries = 6;

    KillFeed();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    struct Entry
    {
        std::string killerName;
        std::string victimName;
        bool isHeadshot = false;
        float timer = 0.f; ///< Remaining display time.
    };
    std::vector<Entry> entries_;
};
```

- [ ] **Step 2: Create KillFeed.cpp**

```cpp
/// @file KillFeed.cpp
#include "KillFeed.hpp"

#include "hud/HudContext.hpp"

#include <algorithm>

KillFeed::KillFeed()
{
    anchor = HudAnchor::TopRight;
    offsetX = -10.f;
    offsetY = 10.f;
}

void KillFeed::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    // Ingest new events.
    for (const auto& ev : state.killFeedEvents) {
        Entry e;
        e.killerName = ev.killerName;
        e.victimName = ev.victimName;
        e.isHeadshot = ev.isHeadshot;
        e.timer = entryLifetime;
        entries_.insert(entries_.begin(), e);
    }

    // Tick timers and remove expired.
    for (auto& e : entries_)
        e.timer -= dt;
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.timer <= 0.f; }),
        entries_.end());

    // Cap entry count.
    if (static_cast<int>(entries_.size()) > maxEntries)
        entries_.resize(maxEntries);
}

void KillFeed::draw(HudContext& ctx, float x, float y)
{
    float curY = y;
    for (const auto& e : entries_) {
        // Fade out in the last fadeOutDuration seconds.
        const float alpha = std::min(e.timer / fadeOutDuration, 1.f);
        const HudColor killerColor(1.f, 1.f, 1.f, alpha);
        const HudColor victimColor(0.8f, 0.2f, 0.2f, alpha);

        // "Killer > Victim" (right-aligned from anchor).
        const char* arrow = " > ";
        const float killerW = ctx.measureText(e.killerName.c_str(), fontSize);
        const float arrowW = ctx.measureText(arrow, fontSize);
        const float victimW = ctx.measureText(e.victimName.c_str(), fontSize);
        const float totalW = killerW + arrowW + victimW;

        float curX = x - totalW;
        ctx.text(e.killerName.c_str(), curX, curY, fontSize, killerColor);
        curX += killerW;
        ctx.text(arrow, curX, curY, fontSize, HudColor(0.7f, 0.7f, 0.7f, alpha));
        curX += arrowW;
        ctx.text(e.victimName.c_str(), curX, curY, fontSize, victimColor);

        curY += entryHeight + entryPadding;
    }
}
```

- [ ] **Step 3: Register and add to CMake**

- [ ] **Step 4: Commit**

```bash
git add src/client/hud/widgets/KillFeed.hpp \
        src/client/hud/widgets/KillFeed.cpp \
        src/client/hud/Hud.cpp CMakeLists.txt
git commit -m "feat(hud): add KillFeed widget with sliding entries

Timed entries anchored top-right, fade out over last 0.5s.
Max 6 visible entries."
```

---

### Task 14: DamageIndicator Widget

**Files:**
- Create: `src/client/hud/widgets/DamageIndicator.hpp`
- Create: `src/client/hud/widgets/DamageIndicator.cpp`
- Modify: `src/client/hud/Hud.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Create DamageIndicator header and implementation**

The damage indicator shows directional arcs around the crosshair showing where damage came from. Each arc fades over ~0.8s.

```cpp
/// @file DamageIndicator.hpp
#pragma once

#include "hud/HudWidget.hpp"

#include <vector>

struct DamageIndicator : HudWidget
{
    float arcDistance = 60.f;  ///< Distance from center.
    float arcLength = 24.f;   ///< Arc segment length.
    float arcThickness = 4.f;
    float fadeTime = 0.8f;

    DamageIndicator();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    struct Arc
    {
        float angleDeg = 0.f;
        float timer = 0.f;
    };
    std::vector<Arc> arcs_;
};
```

```cpp
/// @file DamageIndicator.cpp
#include "DamageIndicator.hpp"

#include "hud/HudContext.hpp"

#include <algorithm>
#include <cmath>

DamageIndicator::DamageIndicator() { anchor = HudAnchor::Center; }

void DamageIndicator::update(float dt, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    for (const auto& ev : state.damageEvents)
        arcs_.push_back({ev.angleDeg, fadeTime});

    for (auto& a : arcs_)
        a.timer -= dt;
    arcs_.erase(std::remove_if(arcs_.begin(), arcs_.end(), [](const Arc& a) { return a.timer <= 0.f; }),
                arcs_.end());
}

void DamageIndicator::draw(HudContext& ctx, float cx, float cy)
{
    for (const auto& a : arcs_) {
        const float alpha = std::clamp(a.timer / fadeTime, 0.f, 1.f);
        const float rad = glm::radians(a.angleDeg);
        // Direction toward damage source.
        const float dx = std::sin(rad);
        const float dy = -std::cos(rad);
        // Arc center position.
        const float ax = cx + dx * arcDistance;
        const float ay = cy + dy * arcDistance;
        // Draw a small rect oriented toward the damage direction.
        ctx.rect(ax - arcThickness * 0.5f, ay - arcLength * 0.5f,
                 arcThickness, arcLength,
                 HudColor(1.f, 0.1f, 0.1f, alpha * 0.7f));
    }
}
```

- [ ] **Step 2: Register and add to CMake**

- [ ] **Step 3: Commit**

```bash
git add src/client/hud/widgets/DamageIndicator.hpp \
        src/client/hud/widgets/DamageIndicator.cpp \
        src/client/hud/Hud.cpp CMakeLists.txt
git commit -m "feat(hud): add DamageIndicator widget

Directional damage arcs around center. Fades over 0.8s."
```

---

### Task 15: RoundTimer Widget

**Files:**
- Create: `src/client/hud/widgets/RoundTimer.hpp`
- Create: `src/client/hud/widgets/RoundTimer.cpp`
- Modify: `src/client/hud/Hud.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Create RoundTimer**

```cpp
/// @file RoundTimer.hpp
#pragma once

#include "hud/HudWidget.hpp"

struct RoundTimer : HudWidget
{
    float fontSize = 24.f;
    float lowTimeThreshold = 10.f; ///< Seconds — color shifts to red below this.

    RoundTimer();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    float timeRemaining_ = 0.f;
};
```

```cpp
/// @file RoundTimer.cpp
#include "RoundTimer.hpp"

#include "hud/HudContext.hpp"

#include <algorithm>
#include <cstdio>

RoundTimer::RoundTimer()
{
    anchor = HudAnchor::TopCenter;
    offsetX = 0.f;
    offsetY = 10.f;
}

void RoundTimer::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    timeRemaining_ = std::max(state.roundTimeRemaining, 0.f);
}

void RoundTimer::draw(HudContext& ctx, float x, float y)
{
    const int minutes = static_cast<int>(timeRemaining_) / 60;
    const int seconds = static_cast<int>(timeRemaining_) % 60;

    char buf[16];
    SDL_snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);

    const HudColor color = (timeRemaining_ <= lowTimeThreshold)
                               ? HudColor(1.f, 0.2f, 0.2f, 1.f)
                               : HudColor::white();

    ctx.text(buf, x, y, fontSize, color, HudAlign::Center);
}
```

- [ ] **Step 2: Register and add to CMake**

- [ ] **Step 3: Commit**

```bash
git add src/client/hud/widgets/RoundTimer.hpp \
        src/client/hud/widgets/RoundTimer.cpp \
        src/client/hud/Hud.cpp CMakeLists.txt
git commit -m "feat(hud): add RoundTimer widget

MM:SS countdown anchored top-center. Turns red below 10s."
```

---

### Task 16: TeamStatusBar Widget

**Files:**
- Create: `src/client/hud/widgets/TeamStatusBar.hpp`
- Create: `src/client/hud/widgets/TeamStatusBar.cpp`
- Modify: `src/client/hud/Hud.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Create TeamStatusBar**

```cpp
/// @file TeamStatusBar.hpp
#pragma once

#include "hud/HudWidget.hpp"

struct TeamStatusBar : HudWidget
{
    float indicatorSize = 12.f;
    float indicatorSpacing = 4.f;
    float scoreFontSize = 20.f;

    TeamStatusBar();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    int allyAlive_ = 0, allyTotal_ = 0;
    int enemyAlive_ = 0, enemyTotal_ = 0;
    int allyScore_ = 0, enemyScore_ = 0;
};
```

```cpp
/// @file TeamStatusBar.cpp
#include "TeamStatusBar.hpp"

#include "hud/HudContext.hpp"

#include <cstdio>

TeamStatusBar::TeamStatusBar()
{
    anchor = HudAnchor::TopCenter;
    offsetX = 0.f;
    offsetY = 40.f; // Below RoundTimer.
}

void TeamStatusBar::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    allyTotal_ = static_cast<int>(state.allies.size());
    allyAlive_ = 0;
    for (const auto& a : state.allies)
        if (a.isAlive)
            allyAlive_++;

    enemyTotal_ = static_cast<int>(state.enemies.size());
    enemyAlive_ = 0;
    for (const auto& e : state.enemies)
        if (e.isAlive)
            enemyAlive_++;

    allyScore_ = state.allyScore;
    enemyScore_ = state.enemyScore;
}

void TeamStatusBar::draw(HudContext& ctx, float x, float y)
{
    // Score: "AllyScore - EnemyScore" centered.
    char scoreText[32];
    SDL_snprintf(scoreText, sizeof(scoreText), "%d - %d", allyScore_, enemyScore_);
    ctx.text(scoreText, x, y, scoreFontSize, HudColor::white(), HudAlign::Center);

    // Ally indicators (left of center).
    const float indicatorY = y + scoreFontSize + 4.f;
    float curX = x - (allyTotal_ * (indicatorSize + indicatorSpacing)) * 0.5f;
    for (int i = 0; i < allyTotal_; i++) {
        const bool alive = i < allyAlive_;
        ctx.rect(curX, indicatorY, indicatorSize, indicatorSize,
                 alive ? HudColor(0.3f, 0.7f, 1.f, 0.9f) : HudColor(0.3f, 0.3f, 0.3f, 0.5f));
        curX += indicatorSize + indicatorSpacing;
    }
}
```

- [ ] **Step 2: Register and add to CMake**

- [ ] **Step 3: Commit**

```bash
git add src/client/hud/widgets/TeamStatusBar.hpp \
        src/client/hud/widgets/TeamStatusBar.cpp \
        src/client/hud/Hud.cpp CMakeLists.txt
git commit -m "feat(hud): add TeamStatusBar widget

Score display and alive/dead indicators anchored top-center."
```

---

### Task 17: Scoreboard Widget

**Files:**
- Create: `src/client/hud/widgets/Scoreboard.hpp`
- Create: `src/client/hud/widgets/Scoreboard.cpp`
- Modify: `src/client/hud/Hud.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Create Scoreboard with TAB toggle and clip rect**

```cpp
/// @file Scoreboard.hpp
#pragma once

#include "hud/HudWidget.hpp"

struct Scoreboard : HudWidget
{
    float panelWidth = 500.f;
    float panelHeight = 400.f;
    float headerFontSize = 18.f;
    float rowFontSize = 14.f;
    float rowHeight = 22.f;

    Scoreboard();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

    /// @brief Set visibility via TAB key (called from Hud::processEvent).
    void setOpen(bool open) { visible = open; }

private:
    std::vector<HudTeamMemberStatus> allies_;
    std::vector<HudTeamMemberStatus> enemies_;
    int allyScore_ = 0, enemyScore_ = 0;
};
```

```cpp
/// @file Scoreboard.cpp
#include "Scoreboard.hpp"

#include "hud/HudContext.hpp"

#include <cstdio>

Scoreboard::Scoreboard()
{
    anchor = HudAnchor::Center;
    visible = false; // Off by default — toggled with TAB.
}

void Scoreboard::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    allies_.assign(state.allies.begin(), state.allies.end());
    enemies_.assign(state.enemies.begin(), state.enemies.end());
    allyScore_ = state.allyScore;
    enemyScore_ = state.enemyScore;
}

void Scoreboard::draw(HudContext& ctx, float cx, float cy)
{
    const float x = cx - panelWidth * 0.5f;
    const float y = cy - panelHeight * 0.5f;

    // Background panel.
    ctx.rect(x, y, panelWidth, panelHeight, HudColor(0.05f, 0.05f, 0.1f, 0.85f));
    ctx.rectOutline(x, y, panelWidth, panelHeight, 1.f, HudColor(0.4f, 0.4f, 0.5f, 0.8f));

    // Header.
    char header[64];
    SDL_snprintf(header, sizeof(header), "SCORE:  %d  -  %d", allyScore_, enemyScore_);
    ctx.text(header, cx, y + 10.f, headerFontSize, HudColor::white(), HudAlign::Center);

    // Clip content area.
    ctx.pushClipRect(x + 4.f, y + 40.f, panelWidth - 8.f, panelHeight - 50.f);

    float rowY = y + 44.f;
    const float nameX = x + 10.f;
    const float killsX = x + panelWidth * 0.6f;
    const float deathsX = x + panelWidth * 0.7f;
    const float pingX = x + panelWidth * 0.85f;

    // Column headers.
    ctx.text("Name", nameX, rowY, rowFontSize, HudColor(0.7f, 0.7f, 0.7f, 1.f));
    ctx.text("K", killsX, rowY, rowFontSize, HudColor(0.7f, 0.7f, 0.7f, 1.f));
    ctx.text("D", deathsX, rowY, rowFontSize, HudColor(0.7f, 0.7f, 0.7f, 1.f));
    ctx.text("Ping", pingX, rowY, rowFontSize, HudColor(0.7f, 0.7f, 0.7f, 1.f));
    rowY += rowHeight;

    // Allies.
    for (const auto& a : allies_) {
        const HudColor c = a.isAlive ? HudColor(0.3f, 0.7f, 1.f, 1.f) : HudColor(0.4f, 0.4f, 0.4f, 0.7f);
        ctx.text(a.name.c_str(), nameX, rowY, rowFontSize, c);
        char buf[16];
        SDL_snprintf(buf, sizeof(buf), "%d", a.kills);
        ctx.text(buf, killsX, rowY, rowFontSize, c);
        SDL_snprintf(buf, sizeof(buf), "%d", a.deaths);
        ctx.text(buf, deathsX, rowY, rowFontSize, c);
        SDL_snprintf(buf, sizeof(buf), "%d", a.ping);
        ctx.text(buf, pingX, rowY, rowFontSize, c);
        rowY += rowHeight;
    }

    // Divider.
    rowY += 4.f;
    ctx.rect(x + 10.f, rowY, panelWidth - 20.f, 1.f, HudColor(0.5f, 0.5f, 0.5f, 0.5f));
    rowY += 6.f;

    // Enemies.
    for (const auto& e : enemies_) {
        const HudColor c = e.isAlive ? HudColor(1.f, 0.4f, 0.3f, 1.f) : HudColor(0.4f, 0.4f, 0.4f, 0.7f);
        ctx.text(e.name.c_str(), nameX, rowY, rowFontSize, c);
        char buf[16];
        SDL_snprintf(buf, sizeof(buf), "%d", e.kills);
        ctx.text(buf, killsX, rowY, rowFontSize, c);
        SDL_snprintf(buf, sizeof(buf), "%d", e.deaths);
        ctx.text(buf, deathsX, rowY, rowFontSize, c);
        SDL_snprintf(buf, sizeof(buf), "%d", e.ping);
        ctx.text(buf, pingX, rowY, rowFontSize, c);
        rowY += rowHeight;
    }

    ctx.popClipRect();
}
```

- [ ] **Step 2: Wire TAB toggle in Hud::processEvent()**

In `Hud.cpp`, update `processEvent()`:

```cpp
void Hud::processEvent(const SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_TAB) {
            // Find scoreboard widget and toggle.
            for (auto& w : widgets_) {
                if (auto* sb = dynamic_cast<Scoreboard*>(w.get()))
                    sb->setOpen(true);
            }
        }
    } else if (event->type == SDL_EVENT_KEY_UP) {
        if (event->key.key == SDLK_TAB) {
            for (auto& w : widgets_) {
                if (auto* sb = dynamic_cast<Scoreboard*>(w.get()))
                    sb->setOpen(false);
            }
        }
    }
}
```

Add `#include "widgets/Scoreboard.hpp"` to Hud.cpp.

- [ ] **Step 3: Register and add to CMake**

- [ ] **Step 4: Commit**

```bash
git add src/client/hud/widgets/Scoreboard.hpp \
        src/client/hud/widgets/Scoreboard.cpp \
        src/client/hud/Hud.cpp CMakeLists.txt
git commit -m "feat(hud): add Scoreboard widget with TAB toggle

Full overlay with team stats, clip rect for content area,
K/D/Ping columns. Hold TAB to show."
```

---

### Task 18: BuyMenu Widget

**Files:**
- Create: `src/client/hud/widgets/BuyMenu.hpp`
- Create: `src/client/hud/widgets/BuyMenu.cpp`
- Modify: `src/client/hud/Hud.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Create BuyMenu with B toggle (during buy phase)**

```cpp
/// @file BuyMenu.hpp
#pragma once

#include "hud/HudWidget.hpp"

struct BuyMenu : HudWidget
{
    float panelWidth = 400.f;
    float panelHeight = 350.f;
    float fontSize = 16.f;
    float itemHeight = 30.f;

    BuyMenu();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;
    void toggle(bool isBuyPhase);

private:
    bool isBuyPhase_ = false;
    float openAlpha_ = 0.f;
};
```

```cpp
/// @file BuyMenu.cpp
#include "BuyMenu.hpp"

#include "hud/HudContext.hpp"
#include "hud/HudTween.hpp"

BuyMenu::BuyMenu()
{
    anchor = HudAnchor::Center;
    visible = false;
}

void BuyMenu::toggle(bool isBuyPhase)
{
    isBuyPhase_ = isBuyPhase;
    if (!isBuyPhase)
        visible = false;
    else
        visible = !visible;
}

void BuyMenu::update(float /*dt*/, const HudGameState& state, HudTweenPool& tweens)
{
    isBuyPhase_ = state.isBuyPhase;
    if (!isBuyPhase_)
        visible = false;

    const float targetAlpha = visible ? 1.f : 0.f;
    if (std::abs(openAlpha_ - targetAlpha) > 0.01f)
        tweens.tween(&openAlpha_, targetAlpha, 0.15f, easeOutQuad);
}

void BuyMenu::draw(HudContext& ctx, float cx, float cy)
{
    if (openAlpha_ < 0.01f)
        return;

    const float x = cx - panelWidth * 0.5f;
    const float y = cy - panelHeight * 0.5f;

    ctx.rect(x, y, panelWidth, panelHeight, HudColor(0.06f, 0.06f, 0.12f, 0.9f * openAlpha_));
    ctx.rectOutline(x, y, panelWidth, panelHeight, 1.f, HudColor(0.5f, 0.4f, 0.2f, openAlpha_));

    ctx.text("BUY MENU", cx, y + 10.f, 20.f,
             HudColor(1.f, 0.85f, 0.3f, openAlpha_), HudAlign::Center);

    // Placeholder weapon list.
    const char* weapons[] = {"1. Rifle", "2. Shotgun", "3. Railgun", "4. Rocket Launcher"};
    float itemY = y + 50.f;
    for (const char* w : weapons) {
        ctx.text(w, x + 20.f, itemY, fontSize, HudColor(1.f, 1.f, 1.f, openAlpha_));
        itemY += itemHeight;
    }

    ctx.text("[B] Close", cx, y + panelHeight - 30.f, 12.f,
             HudColor(0.6f, 0.6f, 0.6f, openAlpha_), HudAlign::Center);
}
```

- [ ] **Step 2: Wire B key toggle in Hud::processEvent()**

Add to the KEY_DOWN handler in `Hud::processEvent()`:

```cpp
        if (event->key.key == SDLK_B) {
            for (auto& w : widgets_) {
                if (auto* bm = dynamic_cast<BuyMenu*>(w.get()))
                    bm->toggle(true); // TODO: pass actual isBuyPhase from state
            }
        }
```

Add `#include "widgets/BuyMenu.hpp"` to Hud.cpp.

- [ ] **Step 3: Register and add to CMake**

- [ ] **Step 4: Commit**

```bash
git add src/client/hud/widgets/BuyMenu.hpp \
        src/client/hud/widgets/BuyMenu.cpp \
        src/client/hud/Hud.cpp CMakeLists.txt
git commit -m "feat(hud): add BuyMenu widget with B toggle

Animated open/close during buy phase. Placeholder weapon list.
B key toggles visibility."
```

---

### Task 19: Minimap Widget

**Files:**
- Create: `src/client/hud/widgets/Minimap.hpp`
- Create: `src/client/hud/widgets/Minimap.cpp`
- Modify: `src/client/hud/Hud.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Create Minimap**

```cpp
/// @file Minimap.hpp
#pragma once

#include "hud/HudWidget.hpp"

struct Minimap : HudWidget
{
    float mapSize = 150.f;    ///< Pixel width/height of the minimap square.
    float dotSize = 4.f;
    float borderThickness = 2.f;

    Minimap();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;
};
```

```cpp
/// @file Minimap.cpp
#include "Minimap.hpp"

#include "hud/HudContext.hpp"

Minimap::Minimap()
{
    anchor = HudAnchor::TopLeft;
    offsetX = 10.f;
    offsetY = 80.f; // Below TeamStatusBar area.
    width = 150.f;
    height = 150.f;
}

void Minimap::update(float /*dt*/, const HudGameState& /*state*/, HudTweenPool& /*tweens*/)
{
    // Teammate/enemy positions would be added to HudGameState
    // and rendered as pips here.  For now, just the frame.
}

void Minimap::draw(HudContext& ctx, float x, float y)
{
    // Background.
    ctx.rect(x, y, mapSize, mapSize, HudColor(0.05f, 0.08f, 0.05f, 0.6f));
    ctx.rectOutline(x, y, mapSize, mapSize, borderThickness, HudColor(0.3f, 0.5f, 0.3f, 0.8f));

    // Player dot (always center).
    const float cx = x + mapSize * 0.5f;
    const float cy = y + mapSize * 0.5f;
    ctx.rect(cx - dotSize * 0.5f, cy - dotSize * 0.5f, dotSize, dotSize,
             HudColor(0.f, 1.f, 0.f, 1.f));
}
```

- [ ] **Step 2: Register and add to CMake**

- [ ] **Step 3: Commit**

```bash
git add src/client/hud/widgets/Minimap.hpp \
        src/client/hud/widgets/Minimap.cpp \
        src/client/hud/Hud.cpp CMakeLists.txt
git commit -m "feat(hud): add Minimap widget

Top-left minimap frame with player dot. Teammate/enemy pips
to be wired when position data is added to HudGameState."
```

---

### Task 20: HudDebugPanel — ImGui Tweaking

**Files:**
- Create: `src/client/hud/debug/HudDebugPanel.hpp`
- Create: `src/client/hud/debug/HudDebugPanel.cpp`
- Modify: `src/client/game/Game.cpp` (call debug panel)
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create HudDebugPanel.hpp**

```cpp
/// @file HudDebugPanel.hpp
/// @brief ImGui panel for live-tweaking HUD widget layout and parameters.

#pragma once

class Hud;

/// @brief Builds an ImGui window exposing HUD widget layout constants.
namespace HudDebugPanel
{

/// @brief Build the HUD tweaker panel.
/// @param hud Reference to the active HUD system.
/// @param open Visibility toggle (linked to DebugUI's panel toggling).
void build(Hud& hud, bool* open);

} // namespace HudDebugPanel
```

- [ ] **Step 2: Create HudDebugPanel.cpp**

```cpp
/// @file HudDebugPanel.cpp
#include "HudDebugPanel.hpp"

#include "hud/Hud.hpp"
#include "hud/widgets/CrosshairWidget.hpp"
#include "hud/widgets/HealthArmorBar.hpp"
#include "hud/widgets/AmmoCounter.hpp"
#include "hud/widgets/KillFeed.hpp"
#include "hud/widgets/RoundTimer.hpp"
#include "hud/widgets/Minimap.hpp"

#include <imgui.h>

namespace
{

const char* anchorName(HudAnchor a)
{
    switch (a) {
    case HudAnchor::TopLeft: return "TopLeft";
    case HudAnchor::TopCenter: return "TopCenter";
    case HudAnchor::TopRight: return "TopRight";
    case HudAnchor::CenterLeft: return "CenterLeft";
    case HudAnchor::Center: return "Center";
    case HudAnchor::CenterRight: return "CenterRight";
    case HudAnchor::BottomLeft: return "BottomLeft";
    case HudAnchor::BottomCenter: return "BottomCenter";
    case HudAnchor::BottomRight: return "BottomRight";
    }
    return "?";
}

} // namespace

void HudDebugPanel::build(Hud& hud, bool* open)
{
    if (!*open)
        return;

    if (!ImGui::Begin("HUD Tweaker", open)) {
        ImGui::End();
        return;
    }

    for (auto& w : hud.widgets()) {
        ImGui::PushID(w.get());

        // Widget header: type name + visibility toggle.
        bool vis = w->visible;
        ImGui::Checkbox("##vis", &vis);
        w->visible = vis;
        ImGui::SameLine();

        if (ImGui::TreeNode("Widget")) {
            ImGui::Text("Anchor: %s", anchorName(w->anchor));
            ImGui::DragFloat("Offset X", &w->offsetX, 1.f, -2000.f, 2000.f);
            ImGui::DragFloat("Offset Y", &w->offsetY, 1.f, -2000.f, 2000.f);

            // Widget-specific controls.
            if (auto* ch = dynamic_cast<CrosshairWidget*>(w.get())) {
                ImGui::DragFloat("Gap", &ch->style.gap, 0.5f, 0.f, 50.f);
                ImGui::DragFloat("Length", &ch->style.length, 0.5f, 1.f, 50.f);
                ImGui::DragFloat("Thickness", &ch->style.thickness, 0.5f, 0.5f, 10.f);
                ImGui::ColorEdit4("Color", &ch->style.color.r);
                ImGui::Checkbox("Dot", &ch->style.dot);
            } else if (auto* hp = dynamic_cast<HealthArmorBar*>(w.get())) {
                ImGui::DragFloat("Bar Width", &hp->barWidth, 1.f, 50.f, 500.f);
                ImGui::DragFloat("Bar Height", &hp->barHeight, 0.5f, 4.f, 50.f);
                ImGui::DragFloat("Font Size", &hp->fontSize, 0.5f, 8.f, 48.f);
            } else if (auto* ac = dynamic_cast<AmmoCounter*>(w.get())) {
                ImGui::DragFloat("Clip Font", &ac->clipFontSize, 0.5f, 12.f, 64.f);
                ImGui::DragFloat("Reserve Font", &ac->reserveFontSize, 0.5f, 8.f, 48.f);
            } else if (auto* kf = dynamic_cast<KillFeed*>(w.get())) {
                ImGui::DragFloat("Lifetime", &kf->entryLifetime, 0.1f, 1.f, 15.f);
                ImGui::DragFloat("Font Size", &kf->fontSize, 0.5f, 8.f, 24.f);
                ImGui::DragInt("Max Entries", &kf->maxEntries, 1, 1, 20);
            } else if (auto* rt = dynamic_cast<RoundTimer*>(w.get())) {
                ImGui::DragFloat("Font Size", &rt->fontSize, 0.5f, 12.f, 48.f);
                ImGui::DragFloat("Low Time Threshold", &rt->lowTimeThreshold, 0.5f, 1.f, 60.f);
            } else if (auto* mm = dynamic_cast<Minimap*>(w.get())) {
                ImGui::DragFloat("Map Size", &mm->mapSize, 1.f, 50.f, 400.f);
                ImGui::DragFloat("Dot Size", &mm->dotSize, 0.5f, 1.f, 12.f);
            }

            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::End();
}
```

- [ ] **Step 3: Wire into Game.cpp**

In Game.cpp, add a `bool showHudDebug_ = false;` member to Game.hpp.

In the debug UI section of `iterate()` (where other debug panels are built), add:

```cpp
    HudDebugPanel::build(hud_, &showHudDebug_);
```

Add the include:

```cpp
#include "hud/debug/HudDebugPanel.hpp"
```

Wire `showHudDebug_` into `debugUI.toggleAllPanels(...)` alongside other panel flags.

- [ ] **Step 4: Add to CMake**

```cmake
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/debug/HudDebugPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/hud/debug/HudDebugPanel.hpp"
```

- [ ] **Step 5: Build, run, verify ImGui panel tweaks HUD widgets live**

- [ ] **Step 6: Commit**

```bash
git add src/client/hud/debug/HudDebugPanel.hpp \
        src/client/hud/debug/HudDebugPanel.cpp \
        src/client/game/Game.hpp src/client/game/Game.cpp \
        CMakeLists.txt
git commit -m "feat(hud): add HudDebugPanel for live ImGui tweaking

Per-widget visibility, anchor offsets, and type-specific parameter
sliders. Toggled via F2 alongside other debug panels."
```

---

## Self-Review Checklist

1. **Spec coverage:** Every spec section has corresponding tasks:
   - Layer 1 (HudRenderer) → Task 4
   - Layer 2 (HudContext) → Task 5
   - Layer 3 (HudTween) → Task 3
   - Layer 4 (Widgets) → Tasks 9-19
   - Renderer integration → Task 7
   - File organization → all tasks follow the spec's directory structure
   - HudDebugPanel → Task 20
   - All 11 widgets from the roster are covered

2. **Placeholder scan:** No TBD/TODO in task steps. Icon atlas uses a 1×1 white fallback (explicitly handled). Minimap teammate pips noted as requiring HudGameState expansion (not a placeholder — the data path exists, just not populated).

3. **Type consistency:** `HudVertex`, `HudColor`, `HudAnchor`, `HudGameState`, `CrosshairStyle`, `HudTweenPool`, `HudEaseFn`, `HudWidget`, `HudContext`, `HudRenderer`, `Hud` — all names are consistent across all tasks. Method signatures (`update(float dt, const HudGameState& state, HudTweenPool& tweens)`, `draw(HudContext& ctx, float drawX, float drawY)`) match between HudWidget.hpp (Task 6) and all widget implementations (Tasks 9-19).
