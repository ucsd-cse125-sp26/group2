# Design: Dear ImGui + Ultralight SDK + EnTT Integration

**Date:** 2026-04-05  
**Status:** Approved — proceeding to implementation

---

## Purpose

Add three dependencies to the project and wire up a minimal proof-of-life demo:

| Library | Version | Role |
|---|---|---|
| **EnTT** | v3.14.0 | Entity-Component-System (always-on; remove USE_ENTT opt-out) |
| **Dear ImGui** | v1.91.6 | Dev/debug overlay (shown in Debug builds, no-op in Release) |
| **Ultralight SDK** | v1.4.0 | Production game UI — menus, settings, HUD (HTML/CSS rendered via SDL3 GPU driver) |

The result is a self-contained **directory** distribution: all shaders + HTML + Ultralight `resources/` are embedded in the binary; Ultralight `.so`/`.dylib`/`.dll` files ship alongside it. A true single-file binary is not achievable because Ultralight only distributes shared libraries.

---

## Architecture

### Object Graph

```
AppState
├── SDL_Window*
├── entt::registry              always-on; no USE_ENTT flag
├── SDLGPURenderer              owns triangle pipeline; draw(SDL_GPURenderPass*) interface
│   └── SDL_GPUDevice*          exposed via device() getter
├── UltralightLayer             production game UI
│   ├── ultralight::Renderer
│   ├── ultralight::View        full-window, transparent background
│   └── SDL3GPUDriver           implements ultralight::GPUDriver
│       ├── texture_map_        SDL_GPUTexture* keyed by UL texture_id
│       ├── geometry_map_       {vertex_buf, index_buf} keyed by UL geometry_id
│       ├── render_buf_map_     SDL_GPUTexture* render targets keyed by UL rb_id
│       ├── fill_pipeline_      SDL3 GPU pipeline for kShaderType_Fill
│       └── fill_path_pipeline_ SDL3 GPU pipeline for kShaderType_FillPath
└── ImGuiLayer                  dev tooling
    ├── imgui_impl_sdl3         input
    └── imgui_impl_sdlgpu3 | imgui_impl_opengl3   render
```

### Render Loop (`SDL_AppIterate`)

```
cmd_buf = SDL_AcquireGPUCommandBuffer(device)

ul_layer.update()                      // UL JS/layout CPU tick
ul_driver.flushCommands(cmd_buf)       // execute stored UL render passes (offscreen)

swapchain = SDL_AcquireGPUSwapchainTexture(cmd_buf, window)
pass = SDL_BeginGPURenderPass(swapchain, CLEAR_BLACK)
  renderer.draw(pass)                  // 3D scene (triangle)
  ul_layer.composite(pass, cmd_buf)    // blit UL view texture → swapchain (premult alpha blend)
  imgui_layer.render(pass, cmd_buf)    // debug overlay (Release = empty call)
SDL_EndGPURenderPass(pass)

SDL_SubmitGPUCommandBuffer(cmd_buf)
```

The critical ordering constraint: Ultralight's offscreen render passes (into its own render buffers) **must complete before** the swapchain render pass begins, because SDL3 GPU does not allow nested render passes.

---

## Dependencies

### EnTT

- Always fetched via FetchContent (no option flag).
- `src/ecs/Registry.hpp` → `using Registry = entt::registry;` — two lines.

### Dear ImGui

- FetchContent from GitHub, tag `v1.91.6`.
- Built as a static CMake library (`imgui` target) from:
  - `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`
  - SDL3 GPU path: `+ imgui_impl_sdl3.cpp + imgui_impl_sdlgpu3.cpp`
  - OpenGL path:   `+ imgui_impl_sdl3.cpp + imgui_impl_opengl3.cpp`
- In Release builds, `ImGuiLayer::newFrame()` and `ImGuiLayer::render()` are no-ops (gated by `#ifndef NDEBUG`).

### Ultralight SDK

- Platform-specific prebuilt SDK downloaded via FetchContent URL.
- `cmake/Ultralight.cmake` handles platform detection + download + imported target creation.
- Three required libraries: `UltralightCore`, `WebCore`, `Ultralight`.
- Post-build: shared libs + `resources/` copied next to the binary.
- `resources/` contents (cacert.pem, fonts) served from memory via a custom `EmbeddedFileSystem` implementing `ultralight::FileSystem` — no `resources/` directory at runtime.
- Ultralight HTML is a C++ string literal — no `.html` file at runtime.

#### Platform Download URLs (v1.4.0)

| Platform | Filename |
|---|---|
| Linux x64 | `ultralight-sdk-linux-x64.tar.gz` |
| macOS arm64 | `ultralight-sdk-mac-arm64.tar.gz` |
| macOS x64 | `ultralight-sdk-mac-x64.tar.gz` |
| Windows x64 | `ultralight-sdk-win-x64.7z` |

Exact URL base: `https://github.com/ultralight-ux/Ultralight/releases/download/v1.4.0/`  
(Verify at runtime; SourceForge mirror available as fallback.)

---

## File Map

### New Files

```
cmake/Ultralight.cmake
src/ui/imgui_layer.hpp
src/ui/imgui_layer.cpp
src/ui/ultralight_layer.hpp
src/ui/ultralight_layer.cpp
src/ui/sdl3gpu_driver.hpp
src/ui/sdl3gpu_driver.cpp
shaders/ultralight/fill.vert         GLSL 450, ported from AppCore
shaders/ultralight/fill.frag
shaders/ultralight/fill_path.vert    (same vert as fill.vert for path format)
shaders/ultralight/fill_path.frag
```

### Modified Files

```
CMakeLists.txt                  deps, source list, UL shaders in compile loop
src/ecs/Registry.hpp            remove stub + USE_ENTT guard
src/main.cpp                    AppState, render loop restructure
src/renderer/IRenderer.hpp      draw(SDL_GPURenderPass*) instead of renderFrame()
src/renderer/SDLGPURenderer.hpp device() getter; split draw from acquire+submit
src/renderer/SDLGPURenderer.cpp same
src/renderer/OpenGLRenderer.hpp update for new interface
src/renderer/OpenGLRenderer.cpp same
scripts/setup-linux.sh          note UL arrives via FetchContent
scripts/setup-archlinux.sh      same
scripts/setup-macos.sh          same
scripts/setup-windows.ps1       same
.github/workflows/ci.yml        FetchContent cache key update
```

---

## SDL3GPUDriver Design

### Data Structures

```cpp
struct GpuTexture {
    SDL_GPUTexture* texture = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    uint32_t width = 0, height = 0;
    bool is_render_target = false;
};

struct GpuGeometry {
    SDL_GPUBuffer* vertex_buf = nullptr;
    SDL_GPUBuffer* index_buf  = nullptr;
    uint32_t vertex_count = 0;
    uint32_t index_count  = 0;
};

struct GpuRenderBuffer {
    uint32_t texture_id = 0;   // references back into texture_map_
};
```

### GPUDriver Interface Implementation

All pure-virtual methods are implemented:

- **Synchronize** — `BeginSynchronize` / `EndSynchronize` clear/flush pending state.
- **Texture** — Create: `SDL_CreateGPUTexture` + `SDL_UploadToGPUTexture`; Update: re-upload; Destroy: `SDL_ReleaseGPUTexture`.
- **RenderBuffer** — `CreateRenderBuffer` creates a texture with `SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET`.
- **Geometry** — Create/Update: `SDL_CreateGPUBuffer` (VERTEX | INDEX usage) + transfer.
- **DrawGeometry** — Sets `current_rb_`, `current_texture_[2]`, draws with active pipelines.
- **UpdateCommandList** — Stores command list copy; `flushCommands()` (called from render loop) iterates and executes each command with one render pass per command.
- **BindTexture / BindRenderBuffer** — Update `current_*` tracked state.

### Ultralight Vertex Format

`Vertex_2f_4ub_2f_2f_28f` (140 bytes/vertex):
```
float pos[2], uint8 color[4], float tex[2], float obj[2],
float data0[4..6] × 7 sets = 28 floats
```

SDL3 GPU vertex attribute descriptors map 11 locations (0–10) to this layout.

### Uniform Strategy

SDL3 GPU's `SDL_PushGPUFragmentUniformData` / `SDL_PushGPUVertexUniformData` route through an internal ring buffer (not Vulkan push constants) and support large payloads. The Ultralight uniform block (~500 bytes: State + Transform + Scalar8 + Vector8 + ClipSize + Clip[8]) is pushed per draw call via this mechanism.

### Fill Shaders (SPIR-V)

Four GLSL 450 files ported from AppCore's embedded headers:

| File | Program |
|---|---|
| `shaders/ultralight/fill.vert` | Quad vertex shader (format `2f_4ub_2f_2f_28f`) |
| `shaders/ultralight/fill.frag` | Fill fragment (solid/gradient/image/RR fills) |
| `shaders/ultralight/fill_path.vert` | Path vertex shader (format `2f_4ub_2f`) |
| `shaders/ultralight/fill_path.frag` | FillPath fragment (vector edge AA) |

Compiled to SPIR-V by the existing build pipeline; added to `SHADER_SOURCES` list in `CMakeLists.txt`. Bundled in Release (`GROUP2_BUNDLE_SHADERS`).

---

## Demo Content

### EnTT

At `SDL_AppInit`:
```cpp
struct Position { float x, y; };
struct Velocity  { float dx, dy; };
auto e0 = reg.create(); reg.emplace<Position>(e0, 640.f, 360.f); reg.emplace<Velocity>(e0,  50.f,  30.f);
auto e1 = reg.create(); reg.emplace<Position>(e1, 200.f, 150.f); reg.emplace<Velocity>(e1, -30.f,  20.f);
auto e2 = reg.create(); reg.emplace<Position>(e2, 900.f, 500.f); reg.emplace<Velocity>(e2,  10.f, -40.f);
SDL_Log("[EnTT] alive=%zu", reg.alive());
```

Updated each frame (position += velocity * dt).

### Ultralight HTML (embedded string)

```html
<html><head>
<style>
body{background:transparent;margin:0;font-family:sans-serif}
.menu{position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);
      background:rgba(0,0,0,.8);border-radius:8px;padding:32px 48px;
      color:#fff;text-align:center;min-width:240px}
h1{margin:0 0 24px;font-size:2em;letter-spacing:.1em}
button{display:block;width:100%;margin:8px 0;padding:10px;background:#333;
       border:1px solid #555;color:#fff;border-radius:4px;font-size:1em;cursor:pointer}
button:hover{background:#555}
</style></head>
<body><div class="menu"><h1>TITANDOOM</h1>
<button>New Game</button><button>Settings</button><button>Quit</button>
</div></body></html>
```

### ImGui Debug Window (Debug builds only)

```
[Debug]
FPS:      142.3
Entities: 3
Backend:  SDL3 GPU
UL View:  loaded
```

---

## Build System Notes

- Ultralight shared libs: `rpath=$ORIGIN` on Linux, `@executable_path` on macOS, DLL copy on Windows.
- `EmbeddedFileSystem` (implementing `ultralight::FileSystem`) serves the bundled `resources/` data from a CMake-generated header (same xxd-based mechanism as shaders).
- CI: `actions/cache@v4` path extended to include `~/.cmake/packages` (already present); the Ultralight SDK tarball download is cached by FetchContent's content hash.
- No new system packages required on any platform.

---

## Out of Scope

- Ultralight JavaScript → C++ callbacks (beyond basic page load)
- ImGui docking / multi-viewport
- Ultralight HTTP/network requests
- Input forwarding from SDL3 to Ultralight View (keyboard/mouse)
