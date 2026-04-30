# HUD System Design

**Date:** 2026-04-29
**Status:** Approved

## Overview

A renderer-agnostic HUD system for a competitive FPS game. The HUD renders to its own offscreen texture, which any renderer (legacy or new) composites as a fullscreen overlay after tone mapping. The HUD system has zero knowledge of which renderer is active, and the renderer has zero knowledge of what's in the HUD.

## Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| ImGui replacement | Custom system | ImGui not customizable enough for shipping HUD |
| Render integration | After tone mapping (LDR) | Predictable colors, effects internal to HUD |
| Decoupling strategy | Offscreen texture blit | Maximum isolation — renderer blits one texture |
| GPU access | Shared SDL_GPUDevice | Same pattern as ParticleSystem, no multi-device |
| Layout | Code-driven + ImGui tweak panel | No JSON config; live tuning via debug panel |
| Text rendering | Shared SdfAtlas data, own renderer | Reuse font atlas, no ParticleSystem dependency |
| Animation | Lightweight tween pool | Fixed-size, no heap allocs, covers FPS HUD needs |
| Architecture | Retained widgets + immediate-mode draw API | Widgets own state/animation, draw layer stays flat |

## Architecture

```
+-------------------------------------------------+
|  Widgets (CrosshairWidget, KillFeed, ...)       |  <- Game state in, draw calls out
+-------------------------------------------------+
|  HudTween                                        |  <- Lightweight animation engine
+-------------------------------------------------+
|  HudContext (immediate-mode draw API)            |  <- Batches quads, text, rects
+-------------------------------------------------+
|  HudRenderer (GPU backend)                       |  <- Owns offscreen target + pipelines
+-------------------------------------------------+
         |
         v  outputs SDL_GPUTexture*
+-------------------------------------------------+
|  Renderer (legacy or new)                        |  <- Blits HUD texture after tonemapping
+-------------------------------------------------+
```

**Ownership:** `Game` owns `Hud`. `Hud` owns `HudRenderer`, `HudContext`, `HudTween` manager, and all widget instances. The renderer receives the output texture via `getOutputTexture()`.

## Layer 1: HudRenderer (GPU Backend)

**Responsibilities:**
- Creates and owns an RGBA8 offscreen render target (matches swapchain resolution)
- Clears the offscreen target to transparent black (0,0,0,0) at the start of each frame
- Owns one GPU pipeline with a unified vertex format (texMode branching in fragment shader)
- Receives flat `HudVertex` array from HudContext each frame, uploads to dynamic vertex buffer
- Handles resolution changes (recreates render target on window resize)

**Vertex format:**

```cpp
struct HudVertex {
    float position[2];  // pixel coords, converted in vertex shader
    float uv[2];        // (0,0) for untextured, atlas UVs for text/icons
    float color[4];     // RGBA, premultiplied alpha
    float texMode;      // 0 = solid color, 1 = SDF text, 2 = sprite, 3 = SDF shape
};
```

Single vertex format for all HUD geometry. Fragment shader branches on `texMode`:
- **0 (solid):** Direct color output
- **1 (SDF text):** Sample SDF atlas, smoothstep for antialiasing
- **2 (sprite):** Sample icon atlas texture
- **3 (SDF shape):** Rounded rect / circle via analytical SDF in fragment shader

**Shaders (2 files):**
- `hud.vert` — pixel-space positions + `screenSize` uniform -> clip space
- `hud.frag` — branches on texMode for solid color, SDF text, sprite sample, or SDF shapes. All alpha-blended.

**Texture strategy:**
- SDF font atlas: shared from SdfAtlas (loaded at startup, R8_UNORM)
- Icon atlas: single texture with crosshair variants, weapon icons, status icons. Loaded once at init.
- Both bound as samplers. `texMode` + UV range selects source.

**Interface to renderer:**

```cpp
SDL_GPUTexture* getOutputTexture() const;
```

One function, one texture. The renderer blits this with alpha blending after tone mapping.

## Layer 2: HudContext (Immediate-Mode Draw API)

The API widgets call into. Accumulates geometry during a frame, then hands it off to HudRenderer.

**API:**

```cpp
// Primitives
void rect(float x, float y, float w, float h, HudColor color);
void rectOutline(float x, float y, float w, float h, float thickness, HudColor color);
void roundedRect(float x, float y, float w, float h, float radius, HudColor color);

// Bars
void bar(float x, float y, float w, float h, float fill01, HudColor fg, HudColor bg);

// Text (SDF)
void text(const char* str, float x, float y, float size, HudColor color,
          HudAlign align = HudAlign::Left);
float measureText(const char* str, float size);

// Icons
void icon(HudIcon id, float x, float y, float size, HudColor tint = White);

// Crosshair (CrosshairStyle: gap, thickness, length, color, dot bool)
void crosshair(const CrosshairStyle& style);

// Clipping
void pushClipRect(float x, float y, float w, float h);
void popClipRect();
```

**Coordinate space:** Pixel coordinates, origin top-left. No implicit DPI scaling at this layer — widget layout handles that.

**Batching:**
- `std::vector<HudVertex>` grows during frame, reset (not deallocated) at frame start
- Quads as 6 vertices each (two triangles), no index buffer (~500-2000 quads max)
- Draw order is submission order — later widgets draw on top
- Clip rects map to GPU scissor rects, triggering draw call breaks only on clip state change
- Typical frame: 1-3 draw calls total

**Rounded rects:** SDF in fragment shader (texMode 3). Vertex shader emits regular quad, fragment shader computes distance to rounded-rect shape. No CPU tessellation.

## Layer 3: HudTween (Animation System)

Lightweight tween engine. No heap allocations, no virtual dispatch.

**Core structure:**

```cpp
struct HudTween {
    float* target;
    float  from, to;
    float  duration, elapsed;
    EaseFn ease;       // float(*)(float t) -> float
    bool   active;
};
```

**Pool:** Fixed-size array (64 tweens). When firing a tween on a target that already has one, the existing tween is replaced (prevents stacking).

**Ease functions (built-in):**
- `easeLinear`, `easeInQuad`, `easeOutQuad`, `easeInOutQuad`
- `easeOutBack` (overshoot — hit markers, score popups)
- `easeOutElastic` (springy effects)

**API:**

```cpp
void tween(float* target, float to, float duration, EaseFn ease = easeOutQuad);
void tween(float* target, float from, float to, float duration, EaseFn ease = easeOutQuad);
void cancel(float* target);
void update(float dt);
```

**Scope boundaries:**
- No sequencing (widgets can chain tweens via completion checks if needed)
- No color tweens — tween individual R/G/B/A floats separately
- No keyframes or state machines

## Layer 4: Widgets

Each widget is a self-contained unit with state, animation, and draw logic.

**Base:**

```cpp
struct HudWidget {
    bool visible = true;

    HudAnchor anchor = HudAnchor::TopLeft;
    float offsetX = 0, offsetY = 0;
    float width = 0, height = 0;

    virtual void update(float dt, const HudGameState& state) = 0;
    virtual void draw(HudContext& ctx) = 0;
    virtual ~HudWidget() = default;
};
```

**HudAnchor:** `TopLeft, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight`. The `Hud` class resolves anchor + offset into pixel coordinates before each widget's `draw()`.

**HudGameState (data contract):**

```cpp
struct HudGameState {
    int   health, maxHealth;
    int   armor, maxArmor;
    int   ammoClip, ammoReserve;
    int   weaponId;
    float roundTimeRemaining;
    bool  isAlive, isBuyPhase;

    // Events (consumed per frame)
    std::span<const KillFeedEntry>     killFeedEvents;
    std::span<const DamageIndicator>   damageEvents;
    std::span<const HitConfirm>        hitConfirms;

    // Team status
    std::span<const TeamMemberStatus>  allies;
    std::span<const TeamMemberStatus>  enemies;
    int allyScore, enemyScore;
};
```

Game fills this struct each frame from ECS data. The HUD never imports ECS headers. Clean data boundary.

**Widget roster:**

| Widget | Anchor | Description |
|--------|--------|-------------|
| `CrosshairWidget` | Center | Dynamic gap, hit marker overlay |
| `HealthArmorBar` | BottomLeft | Two horizontal bars + numeric |
| `AmmoCounter` | BottomRight | Clip / reserve + weapon icon |
| `KillFeed` | TopRight | Sliding entries, timed expiry |
| `DamageIndicator` | Center | Directional arcs showing hit direction |
| `HitMarkerWidget` | Center | Crosshair flare on hit confirm |
| `RoundTimer` | TopCenter | MM:SS countdown, color shift on low time |
| `TeamStatusBar` | TopLeft | Alive/dead indicators per teammate |
| `Scoreboard` | Center | Full overlay, toggled with TAB |
| `BuyMenu` | Center | Grid overlay during buy phase, toggled with B |
| `Minimap` | TopLeft (below team) | Rotated top-down view, teammate/enemy pips |

**Input handling:** `Hud` receives SDL events forwarded from Game (same pattern as `debugUI.processEvent(event)`). Only BuyMenu and Scoreboard consume input. Other widgets are display-only.

**ImGui tweaking:** `HudDebugPanel` iterates over all widgets, exposes anchor, offsets, and widget-specific constants as sliders. Same pattern as existing DebugUI panels.

## Renderer Integration

**IRenderer addition:**

```cpp
virtual void setHudTexture(SDL_GPUTexture* hudOutput) = 0;
```

Called once after HUD init, and again on resize. The renderer stores the pointer and blits it each frame after tone mapping. HybridRenderer routes this like any other feature.

**Blit implementation:** One fullscreen quad with `SrcAlpha / OneMinusSrcAlpha` blending. Single draw call. Premultiplied alpha from HUD texture.

**Initialization order:**

```
Game::init():
  1. SDL window
  2. debugUI.init(window)
  3. renderer.init(window)
  4. hud.init(device, swapchainFormat, shaderFormat, sdfAtlas)
  5. renderer.setHudTexture(hud.getOutputTexture())
  6. particleSystem.init(...)
```

**Frame loop:**

```
Game::iterate():
  1. Input + physics
  2. Build HudGameState from ECS
  3. hud.update(dt, gameState)
  4. hud.render(cmdBuffer)
  5. debugUI.newFrame() + build panels
  6. renderer.drawFrame(...)   // scene + postfx + imgui + HUD blit
```

**Shutdown order:**

```
Game::quit():
  1. renderer.quit()
  2. hud.quit()         // release offscreen target, pipelines, vertex buffers
  3. debugUI.shutdown()
```

## File Organization

```
src/client/hud/
  Hud.hpp / Hud.cpp                     -- Top-level owner, orchestrates widgets
  HudContext.hpp / HudContext.cpp        -- Immediate-mode draw API + batching
  HudRenderer.hpp / HudRenderer.cpp     -- GPU backend, offscreen target, pipelines
  HudTween.hpp / HudTween.cpp           -- Tween pool + easing functions
  HudTypes.hpp                          -- HudColor, HudAnchor, HudAlign, HudVertex, HudGameState
  HudWidget.hpp                         -- Base struct

  widgets/
    CrosshairWidget.hpp / .cpp
    HealthArmorBar.hpp / .cpp
    AmmoCounter.hpp / .cpp
    KillFeed.hpp / .cpp
    DamageIndicator.hpp / .cpp
    HitMarkerWidget.hpp / .cpp
    RoundTimer.hpp / .cpp
    TeamStatusBar.hpp / .cpp
    Scoreboard.hpp / .cpp
    BuyMenu.hpp / .cpp
    Minimap.hpp / .cpp

  debug/
    HudDebugPanel.hpp / .cpp            -- ImGui tweaking panel

shaders/
  hud.vert
  hud.frag
```

**Dependency rules:**
- `hud/` includes only SDL GPU types and `SdfAtlas`/`SdfFont` headers
- No includes from `renderer/`, `ecs/`, `game/`, or `particles/`
- Data flows in via `HudGameState` struct — filled by Game from ECS each frame
- Data flows out via `getOutputTexture()` — consumed by renderer each frame
