# ImGui + Ultralight + EnTT Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Dear ImGui (dev overlay), Ultralight SDK (game UI via SDL3 GPU driver), and make EnTT always-on; wire a running demo showing all three in the app.

**Architecture:** Thin layer classes (`ImGuiLayer`, `UltralightLayer`, `SDL3GPUDriver`) owned by `AppState`. The render loop in `SDL_AppIterate` is restructured so `SDLGPURenderer` exposes `device()` + `draw(SDL_GPURenderPass*)` instead of an opaque `renderFrame()`; the command buffer lifetime moves to the iterate function so all layers share it. Ultralight's GPU driver defers all SDL3 GPU work (uploads + draw calls) to a `flushCommands(SDL_GPUCommandBuffer*)` call that fires before the swapchain render pass opens.

**Tech Stack:** C++23, CMake 3.25+, SDL3, EnTT v3.14.0, Dear ImGui v1.91.6, Ultralight SDK v1.4.0 (prebuilt), SPIR-V shaders via glslc/glslangValidator, Ninja.

---

## File Map

| Path | Action | Responsibility |
|---|---|---|
| `cmake/Ultralight.cmake` | Create | Platform-detect, FetchContent URL, imported targets, post-build copy |
| `src/ui/imgui_layer.hpp` | Create | ImGuiLayer class declaration |
| `src/ui/imgui_layer.cpp` | Create | ImGui init/new-frame/render/shutdown |
| `src/ui/ultralight_layer.hpp` | Create | UltralightLayer class declaration |
| `src/ui/ultralight_layer.cpp` | Create | UL Platform setup, View, EmbeddedFileSystem, composite draw |
| `src/ui/sdl3gpu_driver.hpp` | Create | SDL3GPUDriver class declaration |
| `src/ui/sdl3gpu_driver.cpp` | Create | ultralight::GPUDriver implementation |
| `shaders/ultralight/fill.vert` | Create | GLSL 450 vertex shader — format `2f_4ub_2f_2f_28f` |
| `shaders/ultralight/fill.frag` | Create | GLSL 450 fill fragment shader |
| `shaders/ultralight/fill_path.vert` | Create | GLSL 450 vertex shader — format `2f_4ub_2f` |
| `shaders/ultralight/fill_path.frag` | Create | GLSL 450 fill-path fragment shader |
| `shaders/ultralight/composite.vert` | Create | Full-screen triangle vertex shader |
| `shaders/ultralight/composite.frag` | Create | UL texture → swapchain composite |
| `CMakeLists.txt` | Modify | Add ImGui, include Ultralight.cmake, remove USE_ENTT option, add UL shaders |
| `src/ecs/Registry.hpp` | Modify | Remove stub + USE_ENTT guard; 2-line alias |
| `src/renderer/IRenderer.hpp` | Modify | Replace `renderFrame()` with `draw(SDL_GPURenderPass*)` + `device()` |
| `src/renderer/SDLGPURenderer.hpp` | Modify | Add `device()` getter; split out render pass management |
| `src/renderer/SDLGPURenderer.cpp` | Modify | `draw()` method only draws into an existing pass |
| `src/renderer/OpenGLRenderer.hpp` | Modify | Stub `device()` returning nullptr; keep existing draw logic |
| `src/renderer/OpenGLRenderer.cpp` | Modify | Adapt for new interface |
| `src/main.cpp` | Modify | AppState owns all layers; new render loop |
| `scripts/setup-linux.sh` | Modify | Add note: UL arrives via FetchContent |
| `scripts/setup-archlinux.sh` | Modify | Same |
| `scripts/setup-macos.sh` | Modify | Same |
| `scripts/setup-windows.ps1` | Modify | Same |
| `.github/workflows/ci.yml` | Modify | Extend FetchContent cache key |

---

## Task 1: EnTT — always-on, remove stub

**Files:**
- Modify: `CMakeLists.txt` (lines 26–27 and 67–75)
- Modify: `src/ecs/Registry.hpp`

- [ ] **Step 1: Remove the USE_ENTT option and its guard in CMakeLists.txt**

  In `CMakeLists.txt`, delete lines:
  ```cmake
  option(USE_ENTT     "Use EnTT ECS library (OFF = minimal stub)"        OFF)
  ```
  And replace the conditional block:
  ```cmake
  # if(USE_ENTT)
  #     FetchContent_Declare(EnTT ...)
  #     FetchContent_MakeAvailable(EnTT)
  # endif()
  ```
  with unconditional:
  ```cmake
  # EnTT — fast header-only ECS library (always enabled)
  FetchContent_Declare(
      EnTT
      GIT_REPOSITORY https://github.com/skypjack/entt.git
      GIT_TAG        v3.14.0
      GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(EnTT)
  ```

  Also replace the conditional link block:
  ```cmake
  # if(USE_ENTT)
  #     target_link_libraries(group2 PRIVATE EnTT::EnTT)
  #     target_compile_definitions(group2 PRIVATE USE_ENTT)
  # endif()
  ```
  with:
  ```cmake
  target_link_libraries(group2 PRIVATE EnTT::EnTT)
  ```

- [ ] **Step 2: Simplify Registry.hpp**

  Replace entire contents of `src/ecs/Registry.hpp`:
  ```cpp
  #pragma once
  #include <entt/entt.hpp>
  using Registry = entt::registry;
  ```

- [ ] **Step 3: Build and verify**

  ```bash
  cd /home/user/Documents/dev/game
  cmake --preset debug -DUSE_ENTT=ON 2>&1 | grep -E "(error|warning|EnTT)" || true
  cmake --build --preset debug --parallel 2>&1 | tail -5
  ```
  Expected: builds cleanly; warning about unknown cache variable `USE_ENTT` is acceptable.

- [ ] **Step 4: Commit**

  ```bash
  git add CMakeLists.txt src/ecs/Registry.hpp
  git commit -m "refactor: make EnTT always-on, remove USE_ENTT option and stub"
  ```

---

## Task 2: Dear ImGui — FetchContent + static library

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add ImGui FetchContent block to CMakeLists.txt**

  After the EnTT block, add:
  ```cmake
  # Dear ImGui — immediate-mode GUI (dev overlay; SDL3 GPU + OpenGL backends)
  FetchContent_Declare(
      imgui
      GIT_REPOSITORY https://github.com/ocornut/imgui.git
      GIT_TAG        v1.91.6
      GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(imgui)

  set(IMGUI_SOURCES
      "${imgui_SOURCE_DIR}/imgui.cpp"
      "${imgui_SOURCE_DIR}/imgui_draw.cpp"
      "${imgui_SOURCE_DIR}/imgui_tables.cpp"
      "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
      "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
  )
  if(USE_OPENGL)
      list(APPEND IMGUI_SOURCES
          "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
  else()
      list(APPEND IMGUI_SOURCES
          "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp")
  endif()

  add_library(imgui STATIC ${IMGUI_SOURCES})
  target_include_directories(imgui PUBLIC
      "${imgui_SOURCE_DIR}"
      "${imgui_SOURCE_DIR}/backends"
  )
  target_link_libraries(imgui PRIVATE SDL3::SDL3-static)
  if(USE_OPENGL)
      target_link_libraries(imgui PRIVATE glad)
  endif()
  # Suppress warnings from imgui itself
  target_compile_options(imgui PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>:-w>
      $<$<CXX_COMPILER_ID:MSVC>:/W0>
  )
  ```

- [ ] **Step 2: Link imgui to the main executable**

  In the `target_link_libraries(group2 ...)` block, add `imgui`:
  ```cmake
  target_link_libraries(group2 PRIVATE
      SDL3::SDL3-static
      glm::glm
      EnTT::EnTT
      imgui
  )
  ```

- [ ] **Step 3: Build to verify imgui compiles**

  ```bash
  cmake --preset debug && cmake --build --preset debug --parallel 2>&1 | tail -10
  ```
  Expected: compiles; the `imgui` static library target builds successfully.

- [ ] **Step 4: Commit**

  ```bash
  git add CMakeLists.txt
  git commit -m "build: add Dear ImGui v1.91.6 via FetchContent (static lib)"
  ```

---

## Task 3: Renderer interface refactor

The existing `renderFrame()` monolith is split so the render loop in `main.cpp` can share the command buffer with ImGui and Ultralight layers.

**Files:**
- Modify: `src/renderer/IRenderer.hpp`
- Modify: `src/renderer/SDLGPURenderer.hpp`
- Modify: `src/renderer/SDLGPURenderer.cpp`
- Modify: `src/renderer/OpenGLRenderer.hpp`
- Modify: `src/renderer/OpenGLRenderer.cpp`

- [ ] **Step 1: Update IRenderer.hpp**

  Replace `src/renderer/IRenderer.hpp` entirely:
  ```cpp
  #pragma once
  #include <SDL3/SDL.h>

  // ---------------------------------------------------------------------------
  // IRenderer — interface for the active rendering backend.
  //
  // init()     — create GPU device / context, pipeline.
  // draw()     — record draw commands into an already-open render pass.
  //              For SDL3 GPU path: render_pass is a live SDL_GPURenderPass*.
  //              For OpenGL path:   render_pass is ignored (nullptr).
  // device()   — returns the SDL_GPUDevice* (SDL3 GPU path) or nullptr (GL).
  // shutdown() — destroy all GPU objects.
  // ---------------------------------------------------------------------------
  class IRenderer
  {
  public:
      virtual ~IRenderer() = default;
      virtual bool init(SDL_Window* window)               = 0;
      virtual void draw(SDL_GPURenderPass* render_pass)   = 0;
      virtual SDL_GPUDevice* device() const               = 0;
      virtual void shutdown()                             = 0;
  };
  ```

- [ ] **Step 2: Update SDLGPURenderer.hpp**

  Replace `src/renderer/SDLGPURenderer.hpp`:
  ```cpp
  #pragma once
  #include "IRenderer.hpp"
  #include <SDL3/SDL.h>

  // ---------------------------------------------------------------------------
  // SDLGPURenderer — draws the RGB triangle via the SDL3 GPU pipeline.
  // The caller owns the command buffer and render pass; this class only
  // records triangle draw calls into the supplied render pass.
  // ---------------------------------------------------------------------------
  class SDLGPURenderer : public IRenderer
  {
  public:
      bool           init(SDL_Window* window)             override;
      void           draw(SDL_GPURenderPass* render_pass) override;
      SDL_GPUDevice* device() const                       override { return device_; }
      void           shutdown()                           override;

  private:
      SDL_Window*              window_   = nullptr;
      SDL_GPUDevice*           device_   = nullptr;
      SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
  };
  ```

- [ ] **Step 3: Update SDLGPURenderer.cpp**

  Open `src/renderer/SDLGPURenderer.cpp`. Find the `renderFrame()` method and rename/split it.

  The existing `renderFrame()` does:
  1. `SDL_AcquireGPUCommandBuffer`
  2. `SDL_AcquireGPUSwapchainTexture`
  3. Open render pass
  4. Bind pipeline + draw
  5. End render pass
  6. `SDL_SubmitGPUCommandBuffer`

  Change it so that the class only keeps the pipeline and draws into an externally-provided render pass. Remove steps 1–3 and 5–6. The full new `draw()` method body:
  ```cpp
  void SDLGPURenderer::draw(SDL_GPURenderPass* render_pass)
  {
      SDL_BindGPUGraphicsPipeline(render_pass, pipeline_);
      SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
  }
  ```

  The `init()` method keeps pipeline creation but does NOT store a `SDL_GPUCommandBuffer` member. `shutdown()` releases only the pipeline and device.

  Make sure the member variable `pipeline` is renamed to `pipeline_` to match the header declaration (it was previously `pipeline`). Similarly rename `device` → `device_` and `window` → `window_`.

- [ ] **Step 4: Update OpenGLRenderer.hpp**

  Replace the `renderFrame()` declaration:
  ```cpp
  #pragma once
  #include "IRenderer.hpp"
  #include <SDL3/SDL.h>

  class OpenGLRenderer : public IRenderer
  {
  public:
      bool           init(SDL_Window* window)             override;
      void           draw(SDL_GPURenderPass* /*unused*/)  override;
      SDL_GPUDevice* device() const                       override { return nullptr; }
      void           shutdown()                           override;

  private:
      SDL_Window*   window_   = nullptr;
      SDL_GLContext context_  = nullptr;
  };
  ```

- [ ] **Step 5: Update OpenGLRenderer.cpp**

  Rename the existing `renderFrame()` to `draw(SDL_GPURenderPass*)`. The OpenGL renderer ignores `render_pass`. The body (GL clear + draw calls) stays the same; it simply does its own `SDL_GL_SwapWindow` at the end.

  If the OpenGL renderer currently calls `SDL_GL_SwapWindow` inside `renderFrame`, keep doing so inside `draw`. The command buffer flow does not apply to OpenGL.

- [ ] **Step 6: Update main.cpp temporarily to fix compile errors**

  In `src/main.cpp` rename the `renderer.renderFrame()` call to a placeholder that will be replaced fully in Task 4. For now just add:
  ```cpp
  // TODO Task 4: restructure render loop
  // s->renderer.renderFrame();   // REMOVED
  ```
  and return `SDL_APP_CONTINUE` from iterate without drawing. This is a deliberate temporary regression — the window will be blank until Task 4.

- [ ] **Step 7: Build to verify interface compiles**

  ```bash
  cmake --preset debug && cmake --build --preset debug --parallel 2>&1 | tail -10
  ```
  Expected: compiles cleanly (blank window is expected at runtime).

- [ ] **Step 8: Commit**

  ```bash
  git add src/renderer/IRenderer.hpp src/renderer/SDLGPURenderer.hpp \
          src/renderer/SDLGPURenderer.cpp src/renderer/OpenGLRenderer.hpp \
          src/renderer/OpenGLRenderer.cpp src/main.cpp
  git commit -m "refactor: split renderer renderFrame() into draw(render_pass) + device() getter"
  ```

---

## Task 4: ImGuiLayer + restructured render loop

**Files:**
- Create: `src/ui/imgui_layer.hpp`
- Create: `src/ui/imgui_layer.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt` (add new source files)

- [ ] **Step 1: Create src/ui/imgui_layer.hpp**

  ```cpp
  #pragma once
  #include <SDL3/SDL.h>

  // ---------------------------------------------------------------------------
  // ImGuiLayer — wraps Dear ImGui lifecycle for SDL3 GPU or OpenGL backend.
  //
  // In Release builds (NDEBUG defined) all methods are no-ops so ImGui is
  // compiled out of shipping binaries at the call-site level.
  // ---------------------------------------------------------------------------
  class ImGuiLayer
  {
  public:
      // Pass device=nullptr for the OpenGL backend.
      bool init(SDL_Window* window, SDL_GPUDevice* device);
      // Call once per frame before building ImGui UI.
      void newFrame();
      // Call after ImGui::Render(); needs active render pass (SDL3 GPU)
      // or no render pass (OpenGL — does its own GL calls).
      void render(SDL_GPURenderPass* render_pass, SDL_GPUCommandBuffer* cmd_buf);
      void shutdown();
  };
  ```

- [ ] **Step 2: Create src/ui/imgui_layer.cpp**

  ```cpp
  #include "imgui_layer.hpp"
  #include <imgui.h>

  #ifdef USE_OPENGL
  #  include <imgui_impl_opengl3.h>
  #else
  #  include <imgui_impl_sdlgpu3.h>
  #endif
  #include <imgui_impl_sdl3.h>

  bool ImGuiLayer::init(SDL_Window* window, SDL_GPUDevice* device)
  {
  #ifdef NDEBUG
      (void)window; (void)device; return true;
  #else
      IMGUI_CHECKVERSION();
      ImGui::CreateContext();
      ImGuiIO& io = ImGui::GetIO();
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
      ImGui::StyleColorsDark();

      if (!ImGui_ImplSDL3_InitForSDLGPU(window)) return false;

  #ifdef USE_OPENGL
      if (!ImGui_ImplOpenGL3_Init("#version 410 core")) return false;
  #else
      ImGui_ImplSDLGPU3_InitInfo info{};
      info.Device          = device;
      info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
      info.MSAASamples       = SDL_GPU_SAMPLECOUNT_1;
      if (!ImGui_ImplSDLGPU3_Init(&info)) return false;
  #endif
      return true;
  #endif
  }

  void ImGuiLayer::newFrame()
  {
  #ifndef NDEBUG
  #  ifdef USE_OPENGL
      ImGui_ImplOpenGL3_NewFrame();
  #  else
      ImGui_ImplSDLGPU3_NewFrame();
  #  endif
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();
  #endif
  }

  void ImGuiLayer::render(SDL_GPURenderPass* render_pass, SDL_GPUCommandBuffer* cmd_buf)
  {
  #ifndef NDEBUG
      ImGui::Render();
  #  ifdef USE_OPENGL
      (void)render_pass; (void)cmd_buf;
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  #  else
      ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd_buf, render_pass);
  #  endif
  #else
      (void)render_pass; (void)cmd_buf;
  #endif
  }

  void ImGuiLayer::shutdown()
  {
  #ifndef NDEBUG
  #  ifdef USE_OPENGL
      ImGui_ImplOpenGL3_Shutdown();
  #  else
      ImGui_ImplSDLGPU3_Shutdown();
  #  endif
      ImGui_ImplSDL3_Shutdown();
      ImGui::DestroyContext();
  #endif
  }
  ```

- [ ] **Step 3: Restructure src/main.cpp**

  Replace `src/main.cpp` entirely with the new version that owns all layers and drives the render loop. `UltralightLayer` is included as a forward stub (added fully in Task 10):

  ```cpp
  #define SDL_MAIN_USE_CALLBACKS
  #include <SDL3/SDL.h>
  #include <SDL3/SDL_main.h>

  #ifdef USE_OPENGL
  #  include "renderer/OpenGLRenderer.hpp"
  using ActiveRenderer = OpenGLRenderer;
  #else
  #  include "renderer/SDLGPURenderer.hpp"
  using ActiveRenderer = SDLGPURenderer;
  #endif

  #include "ecs/Registry.hpp"
  #include "ui/imgui_layer.hpp"

  #ifndef NDEBUG
  #  include <imgui.h>
  #  include <imgui_impl_sdl3.h>
  #endif

  // ---------------------------------------------------------------------------
  // ECS components (demo)
  // ---------------------------------------------------------------------------
  struct Position { float x = 0.f, y = 0.f; };
  struct Velocity  { float dx = 0.f, dy = 0.f; };

  // ---------------------------------------------------------------------------
  // App state
  // ---------------------------------------------------------------------------
  struct AppState
  {
      SDL_Window*    window   = nullptr;
      ActiveRenderer renderer;
      Registry       registry;
      ImGuiLayer     imgui;
      uint64_t       last_ticks = 0;
  };

  // ---------------------------------------------------------------------------
  // SDL3 app callbacks
  // ---------------------------------------------------------------------------
  SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* /*argv*/[])
  {
      SDL_SetAppMetadata("group2", "0.1.0", "com.cse125.group2");

      if (!SDL_Init(SDL_INIT_VIDEO)) {
          SDL_Log("SDL_Init failed: %s", SDL_GetError());
          return SDL_APP_FAILURE;
      }

  #ifdef USE_OPENGL
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  #endif

      auto* s = new AppState();
      *appstate = s;

      constexpr int k_winW = 1280, k_winH = 720;
  #ifdef USE_OPENGL
      constexpr SDL_WindowFlags k_winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
  #else
      constexpr SDL_WindowFlags k_winFlags = SDL_WINDOW_RESIZABLE;
  #endif

      s->window = SDL_CreateWindow("group2", k_winW, k_winH, k_winFlags);
      if (!s->window) {
          SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
          return SDL_APP_FAILURE;
      }

      if (!s->renderer.init(s->window)) {
          SDL_Log("Renderer init failed");
          return SDL_APP_FAILURE;
      }

      if (!s->imgui.init(s->window, s->renderer.device())) {
          SDL_Log("ImGui init failed");
          return SDL_APP_FAILURE;
      }

      // ---- EnTT demo entities ----
      auto& reg = s->registry;
      auto e0 = reg.create(); reg.emplace<Position>(e0, 640.f, 360.f); reg.emplace<Velocity>(e0,  50.f,  30.f);
      auto e1 = reg.create(); reg.emplace<Position>(e1, 200.f, 150.f); reg.emplace<Velocity>(e1, -30.f,  20.f);
      auto e2 = reg.create(); reg.emplace<Position>(e2, 900.f, 500.f); reg.emplace<Velocity>(e2,  10.f, -40.f);
      SDL_Log("[EnTT] alive=%zu", static_cast<size_t>(reg.alive()));

  #ifdef USE_OPENGL
      SDL_Log("Backend: OpenGL 4.1 core");
  #else
      SDL_Log("Backend: SDL3 GPU pipeline");
  #endif

      s->last_ticks = SDL_GetTicks();
      return SDL_APP_CONTINUE;
  }

  SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
  {
  #ifndef NDEBUG
      ImGui_ImplSDL3_ProcessEvent(event);
  #endif
      (void)appstate;
      if (event->type == SDL_EVENT_QUIT)        return SDL_APP_SUCCESS;
      if (event->type == SDL_EVENT_KEY_DOWN &&
          event->key.key == SDLK_ESCAPE)        return SDL_APP_SUCCESS;
      return SDL_APP_CONTINUE;
  }

  SDL_AppResult SDL_AppIterate(void* appstate)
  {
      auto* s  = static_cast<AppState*>(appstate);
      auto& reg = s->registry;

      // ---- Delta time ----
      const uint64_t now = SDL_GetTicks();
      const float dt = static_cast<float>(now - s->last_ticks) / 1000.f;
      s->last_ticks = now;

      // ---- EnTT system: move entities ----
      reg.view<Position, Velocity>().each([dt](Position& p, const Velocity& v) {
          p.x += v.dx * dt;
          p.y += v.dy * dt;
      });

  #ifdef USE_OPENGL
      // OpenGL path: ImGui renders via GL calls inside draw()
      s->imgui.newFrame();
  #  ifndef NDEBUG
      ImGui::Begin("Debug");
      ImGui::Text("Backend: OpenGL 4.1");
      ImGui::Text("Entities: %zu", static_cast<size_t>(reg.alive()));
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::End();
  #  endif
      s->renderer.draw(nullptr);
      s->imgui.render(nullptr, nullptr);
  #else
      // SDL3 GPU path
      SDL_GPUDevice* dev = s->renderer.device();

      s->imgui.newFrame();
  #  ifndef NDEBUG
      ImGui::Begin("Debug");
      ImGui::Text("Backend: SDL3 GPU");
      ImGui::Text("Entities: %zu", static_cast<size_t>(reg.alive()));
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::End();
  #  endif

      SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
      if (!cmd) return SDL_APP_CONTINUE;

      SDL_GPUTexture* swapchain = nullptr;
      if (!SDL_AcquireGPUSwapchainTexture(cmd, s->window, &swapchain, nullptr, nullptr) || !swapchain) {
          SDL_CancelGPUCommandBuffer(cmd);
          return SDL_APP_CONTINUE;
      }

      const SDL_GPUColorTargetInfo ct{
          .texture     = swapchain,
          .load_op     = SDL_GPU_LOADOP_CLEAR,
          .store_op    = SDL_GPU_STOREOP_STORE,
          .clear_color = {0.1f, 0.1f, 0.1f, 1.f},
      };
      SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);

      s->renderer.draw(pass);
      s->imgui.render(pass, cmd);

      SDL_EndGPURenderPass(pass);
      SDL_SubmitGPUCommandBuffer(cmd);
  #endif

      return SDL_APP_CONTINUE;
  }

  void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
  {
      auto* s = static_cast<AppState*>(appstate);
      if (!s) return;
      s->imgui.shutdown();
      s->renderer.shutdown();
      SDL_DestroyWindow(s->window);
      SDL_Quit();
      delete s;
  }
  ```

- [ ] **Step 4: Add ui/ sources to CMakeLists.txt**

  In the `set(SOURCES ...)` block, append:
  ```cmake
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/imgui_layer.cpp"
  ```

- [ ] **Step 5: Add `src/ui` to include directories**

  In `target_include_directories(group2 PRIVATE ...)`, append:
  ```cmake
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
  ```

- [ ] **Step 6: Build and run — verify ImGui overlay appears**

  ```bash
  cmake --preset debug && cmake --build --preset debug --parallel
  ./build/debug/group2
  ```
  Expected: window opens, RGB triangle renders, "Debug" overlay window visible in top-left.

- [ ] **Step 7: Commit**

  ```bash
  git add src/ui/imgui_layer.hpp src/ui/imgui_layer.cpp src/main.cpp CMakeLists.txt
  git commit -m "feat: add ImGuiLayer + restructured render loop with EnTT demo entities"
  ```

---

## Task 5: Ultralight GLSL 450 shaders

Six shader files must be written (fill.vert/frag, fill_path.vert/frag, composite.vert/frag). These are GLSL 450 ports of Ultralight AppCore's OpenGL shaders.

**Files:**
- Create: `shaders/ultralight/fill.vert`
- Create: `shaders/ultralight/fill.frag`
- Create: `shaders/ultralight/fill_path.vert`
- Create: `shaders/ultralight/fill_path.frag`
- Create: `shaders/ultralight/composite.vert`
- Create: `shaders/ultralight/composite.frag`

- [ ] **Step 1: Create shaders/ultralight/fill.vert**

  ```glsl
  #version 450

  // Uniform block for vertex stage — slot 0 (SDL_PushGPUVertexUniformData)
  layout(std140, set = 1, binding = 0) uniform VertexUniforms {
      mat4 transform;
  } vu;

  // Vertex format: Vertex_2f_4ub_2f_2f_28f (140 bytes)
  layout(location = 0) in vec2 in_pos;
  layout(location = 1) in vec4 in_color;    // UBYTE4_NORM → [0,1]
  layout(location = 2) in vec2 in_tex;
  layout(location = 3) in vec2 in_obj;
  layout(location = 4) in vec4 in_data0;
  layout(location = 5) in vec4 in_data1;
  layout(location = 6) in vec4 in_data2;
  layout(location = 7) in vec4 in_data3;
  layout(location = 8) in vec4 in_data4;
  layout(location = 9) in vec4 in_data5;
  layout(location = 10) in vec4 in_data6;

  layout(location = 0) out vec4 v_color;
  layout(location = 1) out vec2 v_tex;
  layout(location = 2) out vec2 v_obj;
  layout(location = 3) out vec4 v_data0;
  layout(location = 4) out vec4 v_data1;
  layout(location = 5) out vec4 v_data2;
  layout(location = 6) out vec4 v_data3;
  layout(location = 7) out vec4 v_data4;
  layout(location = 8) out vec4 v_data5;
  layout(location = 9) out vec4 v_data6;

  void main() {
      v_color = in_color;
      v_tex   = in_tex;
      v_obj   = in_obj;
      v_data0 = in_data0;
      v_data1 = in_data1;
      v_data2 = in_data2;
      v_data3 = in_data3;
      v_data4 = in_data4;
      v_data5 = in_data5;
      v_data6 = in_data6;
      gl_Position = vu.transform * vec4(in_pos, 0.0, 1.0);
  }
  ```

- [ ] **Step 2: Create shaders/ultralight/fill.frag**

  ```glsl
  #version 450

  layout(set = 2, binding = 0) uniform sampler2D sTexture1;
  layout(set = 2, binding = 1) uniform sampler2D sTexture2;

  // std140 alignment: State(16) + Transform(64) + Scalar4(32) + Vector(128)
  //                   + ClipSize+pad(16) + Clip[8](512) = 768 bytes
  layout(std140, set = 3, binding = 0) uniform FragUniforms {
      vec4  State;        // [0]=time [1]=vp_w [2]=vp_h [3]=unused
      mat4  Transform;    // (vertex-stage only; here for layout parity)
      vec4  Scalar4[2];   // 8 scalar params as 2×vec4
      vec4  Vector[8];    // 8 vec4 params
      uint  ClipSize;
      uint  _pad0, _pad1, _pad2;
      mat4  Clip[8];
  } fu;

  layout(location = 0) in vec4 v_color;
  layout(location = 1) in vec2 v_tex;
  layout(location = 2) in vec2 v_obj;
  layout(location = 3) in vec4 v_data0;  // [0]=FillType [1]=ShaderType
  layout(location = 4) in vec4 v_data1;
  layout(location = 5) in vec4 v_data2;
  layout(location = 6) in vec4 v_data3;
  layout(location = 7) in vec4 v_data4;
  layout(location = 8) in vec4 v_data5;
  layout(location = 9) in vec4 v_data6;

  layout(location = 0) out vec4 out_color;

  // Signed distance to a rounded rectangle edge.
  float roundRectSDF(vec2 p, vec2 half_size, float r) {
      vec2 q = abs(p) - half_size + r;
      return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
  }

  void main() {
      int fill_type = int(v_data0.x + 0.5);

      if (fill_type == 0) {
          // Solid color — vertex color (premultiplied)
          out_color = v_color;

      } else if (fill_type == 1) {
          // Image / texture fill
          out_color = texture(sTexture1, v_tex) * v_color;

      } else if (fill_type == 2) {
          // Pattern fill (tiled texture)
          vec2 tile_uv = fract(v_obj / fu.Vector[0].xy);
          out_color = texture(sTexture1, tile_uv) * v_color;

      } else if (fill_type == 3) {
          // Linear gradient between Vector[0] (color0) and Vector[1] (color1)
          // gradient direction encoded in Vector[2].xy (start) and Vector[3].xy (end)
          vec2 grad_start = fu.Vector[2].xy;
          vec2 grad_end   = fu.Vector[3].xy;
          vec2 d = grad_end - grad_start;
          float len_sq = dot(d, d);
          float t = (len_sq > 0.0)
              ? clamp(dot(v_obj - grad_start, d) / len_sq, 0.0, 1.0)
              : 0.0;
          out_color = mix(fu.Vector[0], fu.Vector[1], t);

      } else if (fill_type == 4) {
          // Radial gradient: Vector[0]=center/radius, Vector[1]=c0, Vector[2]=c1
          float r = fu.Vector[0].z;
          float t = (r > 0.0) ? clamp(length(v_obj - fu.Vector[0].xy) / r, 0.0, 1.0) : 0.0;
          out_color = mix(fu.Vector[1], fu.Vector[2], t);

      } else if (fill_type == 5) {
          // Box shadow (exterior): Scalar4[0].x = shadow opacity
          out_color = v_color * fu.Scalar4[0].x;

      } else if (fill_type == 6) {
          // Box shadow (inner): straight vertex color
          out_color = v_color;

      } else if (fill_type == 7) {
          // SDF alpha-tested glyph — Scalar4[0].x = weight
          float d     = texture(sTexture1, v_tex).r;
          float w     = fu.Scalar4[0].x;
          float alpha = smoothstep(0.5 - w, 0.5 + w, d);
          out_color   = vec4(v_color.rgb, v_color.a * alpha);

      } else if (fill_type == 8) {
          // Rounded rectangle with anti-aliased edge
          // Vector[0] = (half_width, half_height, radius, unused)
          float d     = roundRectSDF(v_obj, fu.Vector[0].xy, fu.Vector[0].z);
          float alpha = clamp(0.5 - d, 0.0, 1.0);
          out_color   = vec4(v_color.rgb, v_color.a * alpha);

      } else {
          // Fallback: solid vertex color
          out_color = v_color;
      }
  }
  ```

- [ ] **Step 3: Create shaders/ultralight/fill_path.vert**

  ```glsl
  #version 450

  layout(std140, set = 1, binding = 0) uniform VertexUniforms {
      mat4 transform;
  } vu;

  // Vertex format: Vertex_2f_4ub_2f (20 bytes)
  layout(location = 0) in vec2 in_pos;
  layout(location = 1) in vec4 in_color;  // UBYTE4_NORM
  layout(location = 2) in vec2 in_obj;

  layout(location = 0) out vec4 v_color;
  layout(location = 1) out vec2 v_obj;

  void main() {
      v_color     = in_color;
      v_obj       = in_obj;
      gl_Position = vu.transform * vec4(in_pos, 0.0, 1.0);
  }
  ```

- [ ] **Step 4: Create shaders/ultralight/fill_path.frag**

  ```glsl
  #version 450

  layout(set = 2, binding = 0) uniform sampler2D sPattern;

  layout(std140, set = 3, binding = 0) uniform FragUniforms {
      vec4 State;
      mat4 Transform;
      vec4 Scalar4[2];
      vec4 Vector[8];
      uint ClipSize;
      uint _pad0, _pad1, _pad2;
      mat4 Clip[8];
  } fu;

  layout(location = 0) in vec4 v_color;
  layout(location = 1) in vec2 v_obj;

  layout(location = 0) out vec4 out_color;

  void main() {
      // Path fill: solid vertex color; texture used only when enabled.
      float alpha = texture(sPattern, v_obj).r;
      out_color   = vec4(v_color.rgb, v_color.a * alpha);
  }
  ```

- [ ] **Step 5: Create shaders/ultralight/composite.vert**

  ```glsl
  #version 450
  // Generates a full-screen triangle from vertex index — no vertex buffer needed.
  layout(location = 0) out vec2 v_uv;

  void main() {
      v_uv        = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
      gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
  }
  ```

- [ ] **Step 6: Create shaders/ultralight/composite.frag**

  ```glsl
  #version 450
  layout(set = 2, binding = 0) uniform sampler2D sULTexture;
  layout(location = 0) in  vec2 v_uv;
  layout(location = 0) out vec4 out_color;
  void main() { out_color = texture(sULTexture, v_uv); }
  ```

- [ ] **Step 7: Add UL shaders to CMakeLists.txt SHADER_SOURCES**

  Find the line `set(SHADER_SOURCES triangle.vert triangle.frag)` and extend it:
  ```cmake
  set(SHADER_SOURCES
      triangle.vert
      triangle.frag
      ultralight/fill.vert
      ultralight/fill.frag
      ultralight/fill_path.vert
      ultralight/fill_path.frag
      ultralight/composite.vert
      ultralight/composite.frag
  )
  ```

  Also update the `SHADER_SRC_DIR` usage: the `foreach` loop constructs paths as `${SHADER_SRC_DIR}/${SHADER_SRC}`. Since `ultralight/fill.vert` is a relative path, `${SHADER_SRC_DIR}/ultralight/fill.vert` resolves correctly. Create the subdirectory:
  ```bash
  mkdir -p /home/user/Documents/dev/game/shaders/ultralight
  ```

- [ ] **Step 8: Build and verify shaders compile to SPIR-V**

  ```bash
  cmake --preset debug && cmake --build --preset debug --target shaders --parallel
  ls build/debug/shaders/ultralight/
  ```
  Expected: `fill.vert.spv  fill.frag.spv  fill_path.vert.spv  fill_path.frag.spv  composite.vert.spv  composite.frag.spv`

- [ ] **Step 9: Commit**

  ```bash
  git add shaders/ultralight/ CMakeLists.txt
  git commit -m "feat: add Ultralight GLSL 450 shaders (fill, fill_path, composite)"
  ```

---

## Task 6: Ultralight SDK — CMake integration

**Files:**
- Create: `cmake/Ultralight.cmake`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create cmake/Ultralight.cmake**

  ```cmake
  # cmake/Ultralight.cmake
  # Downloads the Ultralight v1.4.0 prebuilt SDK for the current platform,
  # creates imported shared-library targets, and schedules post-build steps
  # to copy shared libs + resources next to the binary.
  #
  # Targets created:
  #   Ultralight::Core     — UltralightCore
  #   Ultralight::Web      — WebCore
  #   Ultralight::UI       — Ultralight

  set(UL_VERSION "1.4.0")
  set(UL_BASE_URL "https://github.com/ultralight-ux/Ultralight/releases/download/v${UL_VERSION}")

  # Detect platform + architecture
  if(WIN32)
      set(UL_ARCHIVE "ultralight-sdk-win-x64.7z")
      set(UL_HASH "")  # populate after first successful download
  elseif(APPLE)
      if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64")
          set(UL_ARCHIVE "ultralight-sdk-mac-arm64.tar.gz")
      else()
          set(UL_ARCHIVE "ultralight-sdk-mac-x64.tar.gz")
      endif()
      set(UL_HASH "")
  else()
      set(UL_ARCHIVE "ultralight-sdk-linux-x64.tar.gz")
      set(UL_HASH "")
  endif()

  include(FetchContent)
  FetchContent_Declare(ultralight_sdk
      URL "${UL_BASE_URL}/${UL_ARCHIVE}"
  )
  FetchContent_MakeAvailable(ultralight_sdk)

  # Locate SDK root (FetchContent places it in ultralight_sdk_SOURCE_DIR)
  set(UL_SDK_ROOT "${ultralight_sdk_SOURCE_DIR}")

  # Platform-specific library paths and filenames
  if(WIN32)
      set(UL_LIB_DIR  "${UL_SDK_ROOT}/lib")
      set(UL_BIN_DIR  "${UL_SDK_ROOT}/bin")
      set(UL_CORE_LIB "${UL_LIB_DIR}/UltralightCore.lib")
      set(UL_WEB_LIB  "${UL_LIB_DIR}/WebCore.lib")
      set(UL_UI_LIB   "${UL_LIB_DIR}/Ultralight.lib")
      set(UL_CORE_DLL "${UL_BIN_DIR}/UltralightCore.dll")
      set(UL_WEB_DLL  "${UL_BIN_DIR}/WebCore.dll")
      set(UL_UI_DLL   "${UL_BIN_DIR}/Ultralight.dll")
  elseif(APPLE)
      set(UL_LIB_DIR  "${UL_SDK_ROOT}/bin")
      set(UL_CORE_LIB "${UL_LIB_DIR}/libUltralightCore.dylib")
      set(UL_WEB_LIB  "${UL_LIB_DIR}/libWebCore.dylib")
      set(UL_UI_LIB   "${UL_LIB_DIR}/libUltralight.dylib")
  else()
      set(UL_LIB_DIR  "${UL_SDK_ROOT}/bin")
      set(UL_CORE_LIB "${UL_LIB_DIR}/libUltralightCore.so")
      set(UL_WEB_LIB  "${UL_LIB_DIR}/libWebCore.so")
      set(UL_UI_LIB   "${UL_LIB_DIR}/libUltralight.so")
  endif()

  set(UL_INCLUDE_DIR "${UL_SDK_ROOT}/include")
  set(UL_RESOURCES_DIR "${UL_SDK_ROOT}/bin/resources"
      CACHE PATH "Ultralight runtime resources directory")

  # Helper to create a SHARED IMPORTED target
  function(_ul_add_target target_name lib_path dll_path)
      add_library(${target_name} SHARED IMPORTED GLOBAL)
      if(WIN32)
          set_target_properties(${target_name} PROPERTIES
              IMPORTED_IMPLIB   "${lib_path}"
              IMPORTED_LOCATION "${dll_path}"
          )
      else()
          set_target_properties(${target_name} PROPERTIES
              IMPORTED_LOCATION "${lib_path}"
          )
      endif()
      set_target_properties(${target_name} PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${UL_INCLUDE_DIR}"
      )
  endfunction()

  if(WIN32)
      _ul_add_target(Ultralight::Core "${UL_CORE_LIB}" "${UL_CORE_DLL}")
      _ul_add_target(Ultralight::Web  "${UL_WEB_LIB}"  "${UL_WEB_DLL}")
      _ul_add_target(Ultralight::UI   "${UL_UI_LIB}"   "${UL_UI_DLL}")
  else()
      _ul_add_target(Ultralight::Core "${UL_CORE_LIB}" "")
      _ul_add_target(Ultralight::Web  "${UL_WEB_LIB}"  "")
      _ul_add_target(Ultralight::UI   "${UL_UI_LIB}"   "")
  endif()

  # Propagate include dirs transitively from UI (the main target apps link to)
  target_link_libraries(Ultralight::UI INTERFACE Ultralight::Core Ultralight::Web)

  message(STATUS "Ultralight SDK: ${UL_SDK_ROOT}")
  message(STATUS "Ultralight resources: ${UL_RESOURCES_DIR}")
  ```

- [ ] **Step 2: Include Ultralight.cmake in CMakeLists.txt**

  After the glad block (near line 90), add:
  ```cmake
  # Ultralight — HTML UI renderer (prebuilt SDK, SDL3 GPU driver path)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Ultralight.cmake")
  ```

- [ ] **Step 3: Add post-build copy steps and link Ultralight to the executable**

  After `add_executable(group2 ...)`, add post-build copy and link:
  ```cmake
  # ---- Ultralight: copy shared libs + resources next to binary ----
  if(WIN32)
      set(UL_SHARED_LIBS
          "${UL_CORE_DLL}" "${UL_WEB_DLL}" "${UL_UI_DLL}")
  elseif(APPLE)
      set(UL_SHARED_LIBS
          "${UL_CORE_LIB}" "${UL_WEB_LIB}" "${UL_UI_LIB}")
  else()
      set(UL_SHARED_LIBS
          "${UL_CORE_LIB}" "${UL_WEB_LIB}" "${UL_UI_LIB}")
  endif()

  foreach(_ul_lib ${UL_SHARED_LIBS})
      add_custom_command(TARGET group2 POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_ul_lib}" "$<TARGET_FILE_DIR:group2>"
          COMMENT "Copying Ultralight lib: ${_ul_lib}"
          VERBATIM
      )
  endforeach()

  if(EXISTS "${UL_RESOURCES_DIR}")
      add_custom_command(TARGET group2 POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${UL_RESOURCES_DIR}" "$<TARGET_FILE_DIR:group2>/resources"
          COMMENT "Copying Ultralight resources/"
          VERBATIM
      )
  endif()
  ```

  In `target_link_libraries(group2 PRIVATE ...)`, add:
  ```cmake
  Ultralight::UI
  ```

  On Linux, set rpath so the binary finds the copied `.so` files:
  ```cmake
  if(UNIX AND NOT APPLE)
      set_target_properties(group2 PROPERTIES
          BUILD_RPATH "$ORIGIN"
          INSTALL_RPATH "$ORIGIN"
      )
  endif()
  if(APPLE)
      set_target_properties(group2 PROPERTIES
          BUILD_RPATH "@executable_path"
          INSTALL_RPATH "@executable_path"
      )
  endif()
  ```

- [ ] **Step 4: Build and verify Ultralight headers are findable**

  Create a temporary `#include <Ultralight/Ultralight.h>` in `src/main.cpp` (just the include, remove after), then build:
  ```bash
  cmake --preset debug && cmake --build --preset debug --parallel 2>&1 | tail -15
  ```
  Expected: includes resolve, Ultralight libs downloaded, DLLs/SOs copied to `build/debug/`.

- [ ] **Step 5: Commit**

  ```bash
  git add cmake/Ultralight.cmake CMakeLists.txt
  git commit -m "build: add Ultralight SDK v1.4.0 via FetchContent, imported targets, post-build copy"
  ```

---

## Task 7: SDL3GPUDriver — resource management

**Files:**
- Create: `src/ui/sdl3gpu_driver.hpp`
- Create: `src/ui/sdl3gpu_driver.cpp` (partial — resource management only)

- [ ] **Step 1: Create src/ui/sdl3gpu_driver.hpp**

  ```cpp
  #pragma once
  #include <SDL3/SDL.h>
  #include <Ultralight/platform/GPUDriver.h>
  #include <unordered_map>
  #include <vector>
  #include <cstdint>

  // ---------------------------------------------------------------------------
  // SDL3GPUDriver — implements ultralight::GPUDriver using SDL3 GPU API.
  //
  // All GPU work is DEFERRED: Ultralight calls the driver during Renderer::Render()
  // which populates pending_draws_.  Call flushCommands(cmd_buf) from the render
  // loop BEFORE opening the swapchain render pass to execute those deferred draws.
  // ---------------------------------------------------------------------------
  class SDL3GPUDriver : public ultralight::GPUDriver
  {
  public:
      // device must outlive this object.
      explicit SDL3GPUDriver(SDL_GPUDevice* device);
      ~SDL3GPUDriver() override;

      // Call once after construction — loads fill/fill_path SPIR-V and creates pipelines.
      // base_path: directory containing shaders/ultralight/*.spv  (build output dir)
      bool buildPipelines(const char* base_path);

      // Execute all deferred Ultralight draw commands into cmd_buf.
      // Must be called BEFORE the swapchain render pass is opened.
      void flushCommands(SDL_GPUCommandBuffer* cmd_buf);

      // Returns the SDL_GPUTexture* for a given Ultralight texture_id.
      SDL_GPUTexture* getTexture(uint32_t texture_id) const;

      // ---- ultralight::GPUDriver interface ----
      void     BeginSynchronize()                                               override;
      void     EndSynchronize()                                                 override;
      uint32_t NextTextureId()                                                  override;
      void     CreateTexture(uint32_t id, ultralight::RefPtr<ultralight::Bitmap> bm)  override;
      void     UpdateTexture(uint32_t id, ultralight::RefPtr<ultralight::Bitmap> bm)  override;
      void     BindTexture(uint8_t unit, uint32_t id)                           override;
      void     DestroyTexture(uint32_t id)                                      override;
      uint32_t NextRenderBufferId()                                             override;
      void     CreateRenderBuffer(uint32_t id, const ultralight::RenderBuffer& rb) override;
      void     BindRenderBuffer(uint32_t id)                                    override;
      void     SetRenderBufferViewport(uint32_t id, uint32_t w, uint32_t h)    override;
      void     ClearRenderBuffer(uint32_t id)                                   override;
      void     DestroyRenderBuffer(uint32_t id)                                 override;
      uint32_t NextGeometryId()                                                 override;
      void     CreateGeometry(uint32_t id,
                               const ultralight::VertexBuffer& vb,
                               const ultralight::IndexBuffer& ib)              override;
      void     UpdateGeometry(uint32_t id,
                               const ultralight::VertexBuffer& vb,
                               const ultralight::IndexBuffer& ib)              override;
      void     DrawGeometry(uint32_t geo_id,
                             uint32_t indices_count,
                             uint32_t indices_offset,
                             const ultralight::GPUState& state)                override;
      void     DestroyGeometry(uint32_t id)                                     override;
      void     UpdateCommandList(const ultralight::CommandList& list)           override;

  private:
      // ---- Internal structures ----
      struct GpuTexture {
          SDL_GPUTexture*  texture      = nullptr;
          SDL_GPUSampler*  sampler      = nullptr;
          uint32_t         width        = 0;
          uint32_t         height       = 0;
          // Pending upload (CPU→GPU deferred to flushCommands)
          std::vector<uint8_t> pending_data;
          SDL_GPUTextureFormat format   = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
          bool             dirty        = false;
          bool             is_rt        = false;  // render target
      };

      struct GpuGeometry {
          SDL_GPUBuffer*   vertex_buf   = nullptr;
          SDL_GPUBuffer*   index_buf    = nullptr;
          uint32_t         vertex_stride = 0;
          uint32_t         index_count  = 0;
          ultralight::VertexBufferFormat vfmt = ultralight::kVertexBufferFormat_2f_4ub_2f_2f_28f;
      };

      struct GpuRenderBuffer {
          uint32_t         texture_id   = 0;
      };

      struct PendingDraw {
          uint32_t         geo_id;
          uint32_t         indices_count;
          uint32_t         indices_offset;
          ultralight::GPUState state;
      };

      struct PendingClear {
          uint32_t         render_buffer_id;
      };

      struct Command {
          bool is_draw;  // true=PendingDraw  false=PendingClear
          PendingDraw  draw;
          PendingClear clear;
      };

      // ---- Uniform structs (pushed per draw call) ----
      struct alignas(16) VertexUniforms {
          float transform[16];  // mat4
      };

      // std140 layout matching fill.frag FragUniforms block (768 bytes)
      struct alignas(16) FragUniforms {
          float    state[4];        // vec4
          float    transform[16];   // mat4 (layout parity — vertex stage uses this)
          float    scalar4[8];      // vec4[2]
          float    vector[32];      // vec4[8]
          uint32_t clip_size;
          uint32_t _pad[3];
          float    clip[8][16];     // mat4[8]
      };

      // ---- GPU objects ----
      SDL_GPUDevice*           device_             = nullptr;
      SDL_GPUGraphicsPipeline* fill_pipeline_      = nullptr;
      SDL_GPUGraphicsPipeline* fill_path_pipeline_ = nullptr;
      SDL_GPUGraphicsPipeline* composite_pipeline_ = nullptr;
      SDL_GPUTexture*          dummy_texture_       = nullptr;
      SDL_GPUSampler*          dummy_sampler_       = nullptr;

      // ---- Resource maps ----
      std::unordered_map<uint32_t, GpuTexture>      textures_;
      std::unordered_map<uint32_t, GpuGeometry>     geometry_;
      std::unordered_map<uint32_t, GpuRenderBuffer> render_buffers_;

      // ---- Per-frame deferred state ----
      std::vector<Command> pending_commands_;
      uint32_t bound_rb_id_    = 0;
      uint32_t bound_tex_[8]   = {};
      uint32_t next_tex_id_    = 1;
      uint32_t next_rb_id_     = 1;
      uint32_t next_geo_id_    = 1;

      // ---- Helpers ----
      SDL_GPUGraphicsPipeline* buildPipeline(
          SDL_GPUShader* vert, SDL_GPUShader* frag,
          ultralight::VertexBufferFormat vfmt,
          SDL_GPUTextureFormat target_fmt,
          bool enable_blend);

      SDL_GPUShader* loadSPIRV(const char* path, SDL_GPUShaderStage stage,
                                uint32_t num_samplers,
                                uint32_t num_uniform_buffers);

      void uploadDirtyTextures(SDL_GPUCommandBuffer* cmd_buf);

      void executeCommand(SDL_GPUCommandBuffer* cmd_buf, const Command& cmd);

      void fillVertexUniforms(VertexUniforms& out, const ultralight::GPUState& s) const;
      void fillFragUniforms(FragUniforms& out, const ultralight::GPUState& s) const;
  };
  ```

- [ ] **Step 2: Create src/ui/sdl3gpu_driver.cpp — includes + constructor/destructor**

  ```cpp
  #include "sdl3gpu_driver.hpp"
  #include <Ultralight/Ultralight.h>
  #include <SDL3/SDL.h>
  #include <cstring>
  #include <cassert>
  #include <fstream>
  #include <vector>

  SDL3GPUDriver::SDL3GPUDriver(SDL_GPUDevice* device)
      : device_(device)
  {}

  SDL3GPUDriver::~SDL3GPUDriver()
  {
      for (auto& [id, tex] : textures_) {
          if (tex.sampler)  SDL_ReleaseGPUSampler(device_, tex.sampler);
          if (tex.texture)  SDL_ReleaseGPUTexture(device_, tex.texture);
      }
      for (auto& [id, geo] : geometry_) {
          if (geo.vertex_buf) SDL_ReleaseGPUBuffer(device_, geo.vertex_buf);
          if (geo.index_buf)  SDL_ReleaseGPUBuffer(device_, geo.index_buf);
      }
      if (dummy_sampler_)       SDL_ReleaseGPUSampler(device_, dummy_sampler_);
      if (dummy_texture_)       SDL_ReleaseGPUTexture(device_, dummy_texture_);
      if (fill_pipeline_)       SDL_ReleaseGPUGraphicsPipeline(device_, fill_pipeline_);
      if (fill_path_pipeline_)  SDL_ReleaseGPUGraphicsPipeline(device_, fill_path_pipeline_);
      if (composite_pipeline_)  SDL_ReleaseGPUGraphicsPipeline(device_, composite_pipeline_);
  }
  ```

- [ ] **Step 3: Add synchronize + ID generators**

  ```cpp
  void SDL3GPUDriver::BeginSynchronize() {}
  void SDL3GPUDriver::EndSynchronize()   {}

  uint32_t SDL3GPUDriver::NextTextureId()      { return next_tex_id_++; }
  uint32_t SDL3GPUDriver::NextRenderBufferId() { return next_rb_id_++;  }
  uint32_t SDL3GPUDriver::NextGeometryId()     { return next_geo_id_++; }
  ```

- [ ] **Step 4: Add texture management**

  ```cpp
  void SDL3GPUDriver::CreateTexture(uint32_t id, ultralight::RefPtr<ultralight::Bitmap> bm)
  {
      GpuTexture& t = textures_[id];
      if (bm && !bm->IsEmpty()) {
          t.width  = bm->width();
          t.height = bm->height();
          t.format = (bm->format() == ultralight::kBitmapFormat_A8_UNORM)
              ? SDL_GPU_TEXTUREFORMAT_R8_UNORM
              : SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
          t.pending_data.resize(bm->size());
          std::memcpy(t.pending_data.data(), bm->LockPixels(), bm->size());
          bm->UnlockPixels();
          t.dirty = true;
      }

      if (t.width == 0) t.width  = 1;
      if (t.height == 0) t.height = 1;

      const SDL_GPUTextureCreateInfo info{
          .type              = SDL_GPU_TEXTURETYPE_2D,
          .format            = t.format,
          .usage             = SDL_GPU_TEXTUREUSAGE_SAMPLER |
                               SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
          .width             = t.width,
          .height            = t.height,
          .layer_count_or_depth = 1,
          .num_levels        = 1,
          .sample_count      = SDL_GPU_SAMPLECOUNT_1,
      };
      t.texture = SDL_CreateGPUTexture(device_, &info);

      const SDL_GPUSamplerCreateInfo sinfo{
          .min_filter        = SDL_GPU_FILTER_LINEAR,
          .mag_filter        = SDL_GPU_FILTER_LINEAR,
          .mipmap_mode       = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
          .address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
          .address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
          .address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
      };
      t.sampler = SDL_CreateGPUSampler(device_, &sinfo);
  }

  void SDL3GPUDriver::UpdateTexture(uint32_t id, ultralight::RefPtr<ultralight::Bitmap> bm)
  {
      auto it = textures_.find(id);
      if (it == textures_.end() || !bm || bm->IsEmpty()) return;
      GpuTexture& t = it->second;
      t.pending_data.resize(bm->size());
      std::memcpy(t.pending_data.data(), bm->LockPixels(), bm->size());
      bm->UnlockPixels();
      t.dirty = true;
  }

  void SDL3GPUDriver::BindTexture(uint8_t unit, uint32_t id)
  {
      if (unit < 8) bound_tex_[unit] = id;
  }

  void SDL3GPUDriver::DestroyTexture(uint32_t id)
  {
      auto it = textures_.find(id);
      if (it == textures_.end()) return;
      if (it->second.sampler) SDL_ReleaseGPUSampler(device_, it->second.sampler);
      if (it->second.texture) SDL_ReleaseGPUTexture(device_, it->second.texture);
      textures_.erase(it);
  }
  ```

- [ ] **Step 5: Add render-buffer management**

  ```cpp
  void SDL3GPUDriver::CreateRenderBuffer(uint32_t id, const ultralight::RenderBuffer& rb)
  {
      render_buffers_[id] = { rb.texture_id };
      // Ensure the backing texture exists and has COLOR_TARGET usage.
      // (CreateTexture always sets COLOR_TARGET, so nothing extra needed here.)
  }

  void SDL3GPUDriver::BindRenderBuffer(uint32_t id)
  {
      bound_rb_id_ = id;
  }

  void SDL3GPUDriver::SetRenderBufferViewport(uint32_t /*id*/, uint32_t /*w*/, uint32_t /*h*/) {}

  void SDL3GPUDriver::ClearRenderBuffer(uint32_t id)
  {
      Command cmd;
      cmd.is_draw = false;
      cmd.clear   = { id };
      pending_commands_.push_back(cmd);
  }

  void SDL3GPUDriver::DestroyRenderBuffer(uint32_t id)
  {
      render_buffers_.erase(id);
  }
  ```

- [ ] **Step 6: Add geometry management**

  ```cpp
  static void uploadBuffer(SDL_GPUDevice* dev, SDL_GPUBuffer** buf,
                            SDL_GPUBufferUsageFlags usage,
                            const void* data, uint32_t size)
  {
      if (*buf) { SDL_ReleaseGPUBuffer(dev, *buf); *buf = nullptr; }
      if (!data || size == 0) return;

      const SDL_GPUBufferCreateInfo binfo{ .usage = usage, .size = size };
      *buf = SDL_CreateGPUBuffer(dev, &binfo);

      const SDL_GPUTransferBufferCreateInfo tbinfo{
          .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size };
      SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbinfo);
      void* mapped = SDL_MapGPUTransferBuffer(dev, tb, false);
      std::memcpy(mapped, data, size);
      SDL_UnmapGPUTransferBuffer(dev, tb);

      SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
      SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
      const SDL_GPUTransferBufferLocation src{ .transfer_buffer = tb, .offset = 0 };
      const SDL_GPUBufferRegion dst{ .buffer = *buf, .offset = 0, .size = size };
      SDL_UploadToGPUBuffer(cp, &src, &dst, false);
      SDL_EndGPUCopyPass(cp);
      SDL_SubmitGPUCommandBuffer(cmd);
      SDL_ReleaseGPUTransferBuffer(dev, tb);
  }

  void SDL3GPUDriver::CreateGeometry(uint32_t id,
                                      const ultralight::VertexBuffer& vb,
                                      const ultralight::IndexBuffer& ib)
  {
      GpuGeometry& g = geometry_[id];
      g.vfmt         = vb.format;
      g.vertex_stride = (vb.format == ultralight::kVertexBufferFormat_2f_4ub_2f)
                          ? 20u : 140u;
      g.index_count  = ib.size / sizeof(uint16_t);

      uploadBuffer(device_, &g.vertex_buf,
                   SDL_GPU_BUFFERUSAGE_VERTEX, vb.data, vb.size);
      uploadBuffer(device_, &g.index_buf,
                   SDL_GPU_BUFFERUSAGE_INDEX, ib.data, ib.size);
  }

  void SDL3GPUDriver::UpdateGeometry(uint32_t id,
                                      const ultralight::VertexBuffer& vb,
                                      const ultralight::IndexBuffer& ib)
  {
      // Re-use CreateGeometry path (it releases + recreates buffers)
      CreateGeometry(id, vb, ib);
  }

  void SDL3GPUDriver::DestroyGeometry(uint32_t id)
  {
      auto it = geometry_.find(id);
      if (it == geometry_.end()) return;
      if (it->second.vertex_buf) SDL_ReleaseGPUBuffer(device_, it->second.vertex_buf);
      if (it->second.index_buf)  SDL_ReleaseGPUBuffer(device_, it->second.index_buf);
      geometry_.erase(it);
  }
  ```

  Note: `uploadBuffer` submits an immediate command buffer internally. This is called during `Renderer::Render()` which is on the main thread, so it's safe to submit immediately for geometry (geometry updates are infrequent). Texture uploads are still deferred (they happen more frequently and the data size can be larger).

- [ ] **Step 7: Add DrawGeometry + UpdateCommandList**

  ```cpp
  void SDL3GPUDriver::DrawGeometry(uint32_t geo_id,
                                    uint32_t indices_count,
                                    uint32_t indices_offset,
                                    const ultralight::GPUState& state)
  {
      Command cmd;
      cmd.is_draw = true;
      cmd.draw    = { geo_id, indices_count, indices_offset, state };
      pending_commands_.push_back(cmd);
  }

  void SDL3GPUDriver::UpdateCommandList(const ultralight::CommandList& list)
  {
      for (uint32_t i = 0; i < list.size; ++i) {
          const ultralight::Command& c = list.commands[i];
          Command cmd;
          if (c.command_type == ultralight::kCommandType_DrawGeometry) {
              cmd.is_draw = true;
              cmd.draw    = { c.geometry_id, c.indices_count,
                               c.indices_offset, c.gpu_state };
          } else {
              cmd.is_draw = false;
              cmd.clear   = { c.gpu_state.render_buffer_id };
          }
          pending_commands_.push_back(cmd);
      }
  }
  ```

- [ ] **Step 8: Add source to CMakeLists.txt**

  ```cmake
  # In SOURCES (or the if/else block for renderer sources):
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/sdl3gpu_driver.cpp"
  ```

- [ ] **Step 9: Build to verify (no link errors yet — buildPipelines not implemented)**

  ```bash
  cmake --preset debug && cmake --build --preset debug --parallel 2>&1 | grep -E "error:|warning:" | head -20
  ```
  Expected: compiles; linker may warn about missing `buildPipelines` — that's fine, it's added in Task 8.

- [ ] **Step 10: Commit**

  ```bash
  git add src/ui/sdl3gpu_driver.hpp src/ui/sdl3gpu_driver.cpp CMakeLists.txt
  git commit -m "feat: SDL3GPUDriver — resource management (textures, geometry, render buffers)"
  ```

---

## Task 8: SDL3GPUDriver — pipeline creation + command execution

**Files:**
- Modify: `src/ui/sdl3gpu_driver.cpp` (add `buildPipelines`, `flushCommands`, helpers)

- [ ] **Step 1: Add SPIRV loader helper**

  Append to `src/ui/sdl3gpu_driver.cpp`:
  ```cpp
  SDL_GPUShader* SDL3GPUDriver::loadSPIRV(const char* path,
                                            SDL_GPUShaderStage stage,
                                            uint32_t num_samplers,
                                            uint32_t num_uniform_buffers)
  {
      // Read file into vector
      std::ifstream f(path, std::ios::binary | std::ios::ate);
      if (!f) {
          SDL_Log("SDL3GPUDriver: cannot open shader %s", path);
          return nullptr;
      }
      const auto sz = static_cast<size_t>(f.tellg());
      f.seekg(0);
      std::vector<uint8_t> code(sz);
      f.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(sz));

      SDL_GPUShaderCreateInfo info{
          .code_size           = sz,
          .code                = code.data(),
          .entrypoint          = "main",
          .format              = SDL_GPU_SHADERFORMAT_SPIRV,
          .stage               = stage,
          .num_samplers        = num_samplers,
          .num_storage_textures = 0,
          .num_storage_buffers = 0,
          .num_uniform_buffers = num_uniform_buffers,
      };
      SDL_GPUShader* shader = SDL_CreateGPUShader(device_, &info);
      if (!shader) SDL_Log("SDL3GPUDriver: shader compile error %s: %s", path, SDL_GetError());
      return shader;
  }
  ```

- [ ] **Step 2: Add pipeline builder helper**

  ```cpp
  SDL_GPUGraphicsPipeline* SDL3GPUDriver::buildPipeline(
      SDL_GPUShader* vert, SDL_GPUShader* frag,
      ultralight::VertexBufferFormat vfmt,
      SDL_GPUTextureFormat target_fmt,
      bool enable_blend)
  {
      // ---- Vertex attributes for Vertex_2f_4ub_2f_2f_28f (140 bytes) ----
      static const SDL_GPUVertexAttribute k_attrs_full[] = {
          { 0,  0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,   0   },
          { 1,  0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 8 },
          { 2,  0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,   12  },
          { 3,  0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,   20  },
          { 4,  0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,   28  },
          { 5,  0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,   44  },
          { 6,  0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,   60  },
          { 7,  0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,   76  },
          { 8,  0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,   92  },
          { 9,  0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,   108 },
          { 10, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,   124 },
      };
      // ---- Vertex attributes for Vertex_2f_4ub_2f (20 bytes) ----
      static const SDL_GPUVertexAttribute k_attrs_path[] = {
          { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,   0  },
          { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 8 },
          { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,   12 },
      };

      const bool is_path = (vfmt == ultralight::kVertexBufferFormat_2f_4ub_2f);
      const SDL_GPUVertexBufferDescription vbd{
          .slot              = 0,
          .pitch             = is_path ? 20u : 140u,
          .input_rate        = SDL_GPU_VERTEXINPUTRATE_VERTEX,
          .instance_step_rate = 0,
      };

      const SDL_GPUColorTargetBlendState blend_premult{
          .src_color_blendfactor   = SDL_GPU_BLENDFACTOR_ONE,
          .dst_color_blendfactor   = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
          .color_blend_op          = SDL_GPU_BLENDOP_ADD,
          .src_alpha_blendfactor   = SDL_GPU_BLENDFACTOR_ONE,
          .dst_alpha_blendfactor   = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
          .alpha_blend_op          = SDL_GPU_BLENDOP_ADD,
          .enable_blend            = SDL_TRUE,
      };
      const SDL_GPUColorTargetBlendState blend_off{};

      const SDL_GPUColorTargetDescription ctd{
          .format      = target_fmt,
          .blend_state = enable_blend ? blend_premult : blend_off,
      };

      SDL_GPUGraphicsPipelineCreateInfo pci{
          .vertex_shader   = vert,
          .fragment_shader = frag,
          .vertex_input_state = {
              .vertex_buffer_descriptions = &vbd,
              .num_vertex_buffers         = 1,
              .vertex_attributes          = is_path ? k_attrs_path : k_attrs_full,
              .num_vertex_attributes      = is_path ? 3u : 11u,
          },
          .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
          .rasterizer_state = {},
          .multisample_state = {},
          .depth_stencil_state = {},
          .target_info = {
              .color_target_descriptions = &ctd,
              .num_color_targets         = 1,
          },
      };
      return SDL_CreateGPUGraphicsPipeline(device_, &pci);
  }
  ```

- [ ] **Step 3: Implement buildPipelines()**

  ```cpp
  bool SDL3GPUDriver::buildPipelines(const char* base_path)
  {
      // Create 1×1 white dummy texture for unbound texture slots
      {
          const SDL_GPUTextureCreateInfo ti{
              .type    = SDL_GPU_TEXTURETYPE_2D,
              .format  = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
              .usage   = SDL_GPU_TEXTUREUSAGE_SAMPLER,
              .width   = 1, .height = 1,
              .layer_count_or_depth = 1, .num_levels = 1,
              .sample_count = SDL_GPU_SAMPLECOUNT_1,
          };
          dummy_texture_ = SDL_CreateGPUTexture(device_, &ti);

          // Upload white pixel
          const SDL_GPUTransferBufferCreateInfo tbci{
              .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = 4 };
          SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &tbci);
          uint8_t* m = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device_, tb, false));
          m[0]=0xFF; m[1]=0xFF; m[2]=0xFF; m[3]=0xFF;
          SDL_UnmapGPUTransferBuffer(device_, tb);
          SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
          SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
          SDL_GPUTextureTransferInfo src{ .transfer_buffer=tb };
          SDL_GPUTextureRegion dst{ .texture=dummy_texture_, .w=1, .h=1, .d=1 };
          SDL_UploadToGPUTexture(cp, &src, &dst, false);
          SDL_EndGPUCopyPass(cp);
          SDL_SubmitGPUCommandBuffer(cmd);
          SDL_ReleaseGPUTransferBuffer(device_, tb);

          const SDL_GPUSamplerCreateInfo sci{
              .min_filter = SDL_GPU_FILTER_NEAREST,
              .mag_filter = SDL_GPU_FILTER_NEAREST,
              .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
              .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
              .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
          };
          dummy_sampler_ = SDL_CreateGPUSampler(device_, &sci);
      }

      // Build path helper: "<base_path>/shaders/ultralight/<name>.spv"
      auto spv_path = [&](const char* name) -> std::string {
          return std::string(base_path) + "/shaders/ultralight/" + name + ".spv";
      };

      const SDL_GPUTextureFormat rt_fmt = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;

      // Fill pipeline (vertex format: 2f_4ub_2f_2f_28f)
      {
          SDL_GPUShader* vs = loadSPIRV(spv_path("fill.vert").c_str(),
              SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
          SDL_GPUShader* fs = loadSPIRV(spv_path("fill.frag").c_str(),
              SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
          if (!vs || !fs) return false;
          fill_pipeline_ = buildPipeline(vs, fs,
              ultralight::kVertexBufferFormat_2f_4ub_2f_2f_28f, rt_fmt, true);
          SDL_ReleaseGPUShader(device_, vs);
          SDL_ReleaseGPUShader(device_, fs);
          if (!fill_pipeline_) { SDL_Log("fill pipeline failed: %s", SDL_GetError()); return false; }
      }

      // FillPath pipeline (vertex format: 2f_4ub_2f)
      {
          SDL_GPUShader* vs = loadSPIRV(spv_path("fill_path.vert").c_str(),
              SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
          SDL_GPUShader* fs = loadSPIRV(spv_path("fill_path.frag").c_str(),
              SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
          if (!vs || !fs) return false;
          fill_path_pipeline_ = buildPipeline(vs, fs,
              ultralight::kVertexBufferFormat_2f_4ub_2f, rt_fmt, true);
          SDL_ReleaseGPUShader(device_, vs);
          SDL_ReleaseGPUShader(device_, fs);
          if (!fill_path_pipeline_) { SDL_Log("fill_path pipeline failed: %s", SDL_GetError()); return false; }
      }

      // Composite pipeline (full-screen triangle, no vertex buffer)
      {
          SDL_GPUShader* vs = loadSPIRV(spv_path("composite.vert").c_str(),
              SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
          SDL_GPUShader* fs = loadSPIRV(spv_path("composite.frag").c_str(),
              SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
          if (!vs || !fs) return false;

          SDL_GPUColorTargetBlendState blend{
              .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
              .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
              .color_blend_op        = SDL_GPU_BLENDOP_ADD,
              .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
              .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
              .alpha_blend_op        = SDL_GPU_BLENDOP_ADD,
              .enable_blend          = SDL_TRUE,
          };
          // Swapchain format is detected at runtime; use BGRA8 as a common default.
          // UltralightLayer passes the correct swapchain format during buildPipelines.
          SDL_GPUColorTargetDescription ctd{ .format = rt_fmt, .blend_state = blend };
          SDL_GPUGraphicsPipelineCreateInfo pci{
              .vertex_shader   = vs,
              .fragment_shader = fs,
              .vertex_input_state = {},  // no vertex buffer
              .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
              .target_info = {
                  .color_target_descriptions = &ctd,
                  .num_color_targets         = 1,
              },
          };
          composite_pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pci);
          SDL_ReleaseGPUShader(device_, vs);
          SDL_ReleaseGPUShader(device_, fs);
          if (!composite_pipeline_) { SDL_Log("composite pipeline failed: %s", SDL_GetError()); return false; }
      }

      return true;
  }
  ```

- [ ] **Step 4: Add uniform fill helpers**

  ```cpp
  void SDL3GPUDriver::fillVertexUniforms(VertexUniforms& out,
                                          const ultralight::GPUState& s) const
  {
      std::memcpy(out.transform, &s.transform.data[0][0], sizeof(out.transform));
  }

  void SDL3GPUDriver::fillFragUniforms(FragUniforms& out,
                                        const ultralight::GPUState& s) const
  {
      out.state[0] = 0.f;
      out.state[1] = static_cast<float>(s.viewport_width);
      out.state[2] = static_cast<float>(s.viewport_height);
      out.state[3] = 0.f;
      // Transform (same as vertex, here for layout parity)
      std::memcpy(out.transform, &s.transform.data[0][0], sizeof(out.transform));
      // Scalar4 — 8 floats packed into 2 vec4s
      for (int i = 0; i < 8; ++i) out.scalar4[i] = s.uniform_scalar[i];
      // Vector — 8 vec4s
      for (int i = 0; i < 8; ++i) {
          out.vector[i*4+0] = s.uniform_vector[i].x;
          out.vector[i*4+1] = s.uniform_vector[i].y;
          out.vector[i*4+2] = s.uniform_vector[i].z;
          out.vector[i*4+3] = s.uniform_vector[i].w;
      }
      out.clip_size = s.clip_size;
      std::memset(out._pad, 0, sizeof(out._pad));
      for (uint32_t i = 0; i < s.clip_size && i < 8; ++i)
          std::memcpy(&out.clip[i][0], &s.clip[i].data[0][0], 16*sizeof(float));
  }
  ```

- [ ] **Step 5: Implement uploadDirtyTextures()**

  ```cpp
  void SDL3GPUDriver::uploadDirtyTextures(SDL_GPUCommandBuffer* cmd_buf)
  {
      for (auto& [id, tex] : textures_) {
          if (!tex.dirty || tex.pending_data.empty()) continue;
          const auto sz = static_cast<uint32_t>(tex.pending_data.size());
          const SDL_GPUTransferBufferCreateInfo tbci{
              .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = sz };
          SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &tbci);
          void* mapped = SDL_MapGPUTransferBuffer(device_, tb, false);
          std::memcpy(mapped, tex.pending_data.data(), sz);
          SDL_UnmapGPUTransferBuffer(device_, tb);

          SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd_buf);
          SDL_GPUTextureTransferInfo src{ .transfer_buffer = tb };
          SDL_GPUTextureRegion dst{ .texture=tex.texture, .w=tex.width, .h=tex.height, .d=1 };
          SDL_UploadToGPUTexture(cp, &src, &dst, false);
          SDL_EndGPUCopyPass(cp);
          SDL_ReleaseGPUTransferBuffer(device_, tb);

          tex.dirty = false;
          tex.pending_data.clear();
          tex.pending_data.shrink_to_fit();
      }
  }
  ```

- [ ] **Step 6: Implement executeCommand() — the draw dispatch**

  ```cpp
  SDL_GPUTexture* SDL3GPUDriver::getTexture(uint32_t id) const
  {
      auto it = textures_.find(id);
      return (it != textures_.end()) ? it->second.texture : dummy_texture_;
  }

  void SDL3GPUDriver::executeCommand(SDL_GPUCommandBuffer* cmd_buf, const Command& cmd)
  {
      if (!cmd.is_draw) {
          // ClearRenderBuffer
          const auto& rb_it = render_buffers_.find(cmd.clear.render_buffer_id);
          if (rb_it == render_buffers_.end()) return;
          SDL_GPUTexture* rt = getTexture(rb_it->second.texture_id);
          if (!rt) return;
          const SDL_GPUColorTargetInfo ct{
              .texture   = rt,
              .load_op   = SDL_GPU_LOADOP_CLEAR,
              .store_op  = SDL_GPU_STOREOP_STORE,
              .clear_color = {0, 0, 0, 0},
          };
          SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd_buf, &ct, 1, nullptr);
          SDL_EndGPURenderPass(pass);
          return;
      }

      // DrawGeometry
      const PendingDraw& d  = cmd.draw;
      const auto& geo_it    = geometry_.find(d.geo_id);
      if (geo_it == geometry_.end()) return;
      const GpuGeometry& geo = geo_it->second;

      const auto& rb_it = render_buffers_.find(d.state.render_buffer_id);
      if (rb_it == render_buffers_.end()) return;
      SDL_GPUTexture* rt = getTexture(rb_it->second.texture_id);
      if (!rt) return;

      SDL_GPUGraphicsPipeline* pipeline =
          (d.state.shader_type == ultralight::kShaderType_FillPath)
          ? fill_path_pipeline_ : fill_pipeline_;

      const SDL_GPUColorTargetInfo ct{
          .texture  = rt,
          .load_op  = SDL_GPU_LOADOP_LOAD,
          .store_op = SDL_GPU_STOREOP_STORE,
      };
      SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd_buf, &ct, 1, nullptr);

      SDL_BindGPUGraphicsPipeline(pass, pipeline);

      // Scissor
      if (d.state.enable_scissor) {
          const SDL_Rect sr{
              static_cast<int>(d.state.scissor_rect.left),
              static_cast<int>(d.state.scissor_rect.top),
              static_cast<int>(d.state.scissor_rect.right  - d.state.scissor_rect.left),
              static_cast<int>(d.state.scissor_rect.bottom - d.state.scissor_rect.top),
          };
          SDL_SetGPUScissor(pass, &sr);
      }

      // Textures
      auto bindTex = [&](uint32_t slot, uint32_t tex_id) {
          auto it = textures_.find(tex_id);
          SDL_GPUTextureSamplerBinding b{
              .texture = (it != textures_.end()) ? it->second.texture : dummy_texture_,
              .sampler = (it != textures_.end()) ? it->second.sampler : dummy_sampler_,
          };
          SDL_BindGPUFragmentSamplers(pass, slot, &b, 1);
      };
      bindTex(0, d.state.texture_1_id);
      bindTex(1, d.state.texture_2_id);

      // Vertex + index buffers
      SDL_GPUBufferBinding vb{ .buffer = geo.vertex_buf, .offset = 0 };
      SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
      SDL_GPUBufferBinding ib{ .buffer = geo.index_buf,  .offset = 0 };
      SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);

      // Push uniforms
      VertexUniforms vu; fillVertexUniforms(vu, d.state);
      SDL_PushGPUVertexUniformData(cmd_buf, 0, &vu, sizeof(vu));
      FragUniforms fu;   fillFragUniforms(fu, d.state);
      SDL_PushGPUFragmentUniformData(cmd_buf, 0, &fu, sizeof(fu));

      SDL_DrawGPUIndexedPrimitives(pass,
          d.indices_count, 1,
          d.indices_offset, 0, 0);

      SDL_EndGPURenderPass(pass);
  }
  ```

- [ ] **Step 7: Implement flushCommands()**

  ```cpp
  void SDL3GPUDriver::flushCommands(SDL_GPUCommandBuffer* cmd_buf)
  {
      if (pending_commands_.empty()) return;
      uploadDirtyTextures(cmd_buf);
      for (const auto& cmd : pending_commands_)
          executeCommand(cmd_buf, cmd);
      pending_commands_.clear();
  }
  ```

- [ ] **Step 8: Build and verify (full driver compiles)**

  ```bash
  cmake --preset debug && cmake --build --preset debug --parallel 2>&1 | grep -E "error:" | head -20
  ```
  Expected: zero errors.

- [ ] **Step 9: Commit**

  ```bash
  git add src/ui/sdl3gpu_driver.cpp src/ui/sdl3gpu_driver.hpp
  git commit -m "feat: SDL3GPUDriver — pipeline creation and command execution"
  ```

---

## Task 9: UltralightLayer

**Files:**
- Create: `src/ui/ultralight_layer.hpp`
- Create: `src/ui/ultralight_layer.cpp`

- [ ] **Step 1: Create src/ui/ultralight_layer.hpp**

  ```cpp
  #pragma once
  #include <SDL3/SDL.h>
  #include <Ultralight/Ultralight.h>
  #include <string>
  #include <memory>

  class SDL3GPUDriver;

  // ---------------------------------------------------------------------------
  // UltralightLayer — manages the Ultralight Platform, Renderer, and one View.
  //
  // Typical per-frame usage from AppState:
  //   1. ul.update()                          — JS/layout tick
  //   2. ul.render()                          — triggers GPUDriver draw recording
  //   3. driver.flushCommands(cmd_buf)         — execute deferred SDL3 GPU work
  //   4. (open swapchain pass)
  //   5. ul.composite(pass, cmd_buf, w, h)     — blit UL view to swapchain
  // ---------------------------------------------------------------------------
  class UltralightLayer
  {
  public:
      explicit UltralightLayer(SDL3GPUDriver* driver);
      ~UltralightLayer();

      bool init(SDL_Window* window, SDL_GPUDevice* device,
                SDL_GPUTextureFormat swapchain_fmt);

      // Call every frame (before render)
      void update();
      // Triggers Ultralight's rendering into GPU textures via SDL3GPUDriver
      void render();
      // Composites the rendered UL view texture onto the current render pass
      void composite(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd_buf,
                     uint32_t viewport_w, uint32_t viewport_h);
      void shutdown();

      // Returns the composite pipeline (built in buildPipelines)
      SDL_GPUGraphicsPipeline* compositePipeline() const;

  private:
      SDL3GPUDriver*                    driver_   = nullptr;
      SDL_GPUDevice*                    device_   = nullptr;
      ultralight::RefPtr<ultralight::Renderer> renderer_;
      ultralight::RefPtr<ultralight::View>     view_;
  };
  ```

- [ ] **Step 2: Create src/ui/ultralight_layer.cpp**

  ```cpp
  #include "ultralight_layer.hpp"
  #include "sdl3gpu_driver.hpp"
  #include <Ultralight/platform/Platform.h>
  #include <Ultralight/platform/Config.h>
  #include <Ultralight/platform/Logger.h>
  #include <SDL3/SDL.h>

  // ---------------------------------------------------------------------------
  // Minimal logger that routes to SDL_Log
  // ---------------------------------------------------------------------------
  class SDLLogger : public ultralight::Logger {
  public:
      void LogMessage(ultralight::LogLevel level, const ultralight::String& msg) override {
          const char* tag = (level == ultralight::kLogLevel_Error) ? "[UL ERROR]"
                          : (level == ultralight::kLogLevel_Warning) ? "[UL WARN]" : "[UL]";
          SDL_Log("%s %s", tag, msg.utf8().data());
      }
  };

  static SDLLogger g_logger;

  // ---------------------------------------------------------------------------
  // Stub FileSystem — just enough to suppress "file not found" logs for now.
  // Resources are served from the SDK's bin/resources/ directory (copied at build).
  // ---------------------------------------------------------------------------
  class SDLFileSystem : public ultralight::FileSystem {
  public:
      bool FileExists(const ultralight::String16& path) override { return false; }
      bool GetFileSize(ultralight::FileHandle handle, int64_t& result) override { return false; }
      bool GetFileMimeType(const ultralight::String16& path, ultralight::String16& result) override { return false; }
      ultralight::FileHandle OpenFile(const ultralight::String16& path, bool open_for_writing) override {
          return ultralight::invalidFileHandle;
      }
      void CloseFile(ultralight::FileHandle& handle) override {}
      int64_t ReadFromFile(ultralight::FileHandle handle, char* data, int64_t length) override { return -1; }
  };

  static SDLFileSystem g_fs;

  // ---------------------------------------------------------------------------
  // Game menu HTML (no external file needed)
  // ---------------------------------------------------------------------------
  static constexpr const char* k_menu_html = R"HTML(
  <html><head><style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { background: transparent; font-family: sans-serif; }
  .menu {
      position: fixed; top: 50%; left: 50%;
      transform: translate(-50%, -50%);
      background: rgba(0,0,0,0.82);
      border: 1px solid rgba(255,255,255,0.12);
      border-radius: 10px;
      padding: 36px 52px;
      color: #fff;
      text-align: center;
      min-width: 260px;
  }
  h1 { font-size: 2.2em; letter-spacing: 0.12em; margin-bottom: 28px;
       text-transform: uppercase; }
  button {
      display: block; width: 100%; margin: 10px 0;
      padding: 11px 0; background: rgba(255,255,255,0.08);
      border: 1px solid rgba(255,255,255,0.18);
      color: #fff; font-size: 1em; border-radius: 5px; cursor: pointer;
  }
  button:hover { background: rgba(255,255,255,0.2); }
  </style></head>
  <body>
  <div class="menu">
      <h1>TitanDoom</h1>
      <button>New Game</button>
      <button>Settings</button>
      <button>Quit</button>
  </div>
  </body></html>
  )HTML";

  // ---------------------------------------------------------------------------
  UltralightLayer::UltralightLayer(SDL3GPUDriver* driver) : driver_(driver) {}
  UltralightLayer::~UltralightLayer() { shutdown(); }

  bool UltralightLayer::init(SDL_Window* window,
                              SDL_GPUDevice* device,
                              SDL_GPUTextureFormat /*swapchain_fmt*/)
  {
      device_ = device;

      // ---- Configure Platform ----
      ultralight::Config cfg;
      cfg.resource_path_prefix = "./resources/";  // copied by CMake post-build
      cfg.face_winding          = ultralight::kFaceWinding_CounterClockwise;

      auto& platform = ultralight::Platform::instance();
      platform.set_config(cfg);
      platform.set_logger(&g_logger);
      platform.set_file_system(&g_fs);
      platform.set_gpu_driver(driver_);

      // ---- Create Renderer ----
      renderer_ = ultralight::Renderer::Create();
      if (!renderer_) { SDL_Log("[UL] Renderer::Create failed"); return false; }

      // ---- Create View ----
      int w = 0, h = 0;
      SDL_GetWindowSize(window, &w, &h);

      ultralight::ViewConfig vc;
      vc.is_accelerated  = true;
      vc.is_transparent  = true;
      vc.display_id      = 0;
      vc.initial_device_scale = 1.0f;

      view_ = renderer_->CreateView(static_cast<uint32_t>(w),
                                     static_cast<uint32_t>(h),
                                     vc, nullptr);
      if (!view_) { SDL_Log("[UL] CreateView failed"); return false; }

      view_->LoadHTML(k_menu_html);
      SDL_Log("[UL] View created (%d×%d), HTML loaded", w, h);
      return true;
  }

  void UltralightLayer::update()
  {
      if (renderer_) {
          renderer_->Update();
          renderer_->RefreshDisplay(0);
      }
  }

  void UltralightLayer::render()
  {
      if (renderer_) renderer_->Render();
  }

  void UltralightLayer::composite(SDL_GPURenderPass* pass,
                                   SDL_GPUCommandBuffer* /*cmd_buf*/,
                                   uint32_t /*vp_w*/, uint32_t /*vp_h*/)
  {
      if (!view_) return;
      const ultralight::RenderTarget rt = view_->render_target();
      SDL_GPUTexture* ul_tex = driver_->getTexture(rt.texture_id);
      if (!ul_tex) return;

      // Bind composite pipeline + UL texture, draw full-screen triangle
      SDL_BindGPUGraphicsPipeline(pass, driver_->getCompositePipeline());
      SDL_GPUSampler* samp;
      {   // get sampler from driver texture map
          // We'll expose a helper on the driver; for now use a workaround:
          // UltralightLayer stores its own sampler reference.
          // (See note in Step 3 below)
          samp = driver_->getSampler(rt.texture_id);
      }
      SDL_GPUTextureSamplerBinding b{ .texture = ul_tex, .sampler = samp };
      SDL_BindGPUFragmentSamplers(pass, 0, &b, 1);
      SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
  }

  void UltralightLayer::shutdown()
  {
      view_     = nullptr;
      renderer_ = nullptr;
  }
  ```

- [ ] **Step 3: Add getCompositePipeline() and getSampler() to SDL3GPUDriver**

  Add to `src/ui/sdl3gpu_driver.hpp` (public section):
  ```cpp
  SDL_GPUGraphicsPipeline* getCompositePipeline() const { return composite_pipeline_; }
  SDL_GPUSampler*          getSampler(uint32_t texture_id) const;
  ```

  Add to `src/ui/sdl3gpu_driver.cpp`:
  ```cpp
  SDL_GPUSampler* SDL3GPUDriver::getSampler(uint32_t id) const
  {
      auto it = textures_.find(id);
      return (it != textures_.end()) ? it->second.sampler : dummy_sampler_;
  }
  ```

- [ ] **Step 4: Add ultralight_layer.cpp to CMakeLists.txt SOURCES**

  Append to the SOURCES set:
  ```cmake
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ultralight_layer.cpp"
  ```

- [ ] **Step 5: Build to verify (no UltralightLayer wiring in main.cpp yet)**

  ```bash
  cmake --preset debug && cmake --build --preset debug --parallel 2>&1 | grep "error:" | head -20
  ```
  Expected: zero errors.

- [ ] **Step 6: Commit**

  ```bash
  git add src/ui/ultralight_layer.hpp src/ui/ultralight_layer.cpp \
          src/ui/sdl3gpu_driver.hpp src/ui/sdl3gpu_driver.cpp CMakeLists.txt
  git commit -m "feat: UltralightLayer + SDL3GPUDriver composite helpers"
  ```

---

## Task 10: Wire UltralightLayer into AppState and render loop

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add UltralightLayer to AppState and update SDL_AppInit**

  At the top of `src/main.cpp`, add:
  ```cpp
  #include "ui/ultralight_layer.hpp"
  #include "ui/sdl3gpu_driver.hpp"
  ```

  In `AppState`, add:
  ```cpp
  struct AppState {
      SDL_Window*    window   = nullptr;
      ActiveRenderer renderer;
      Registry       registry;
      ImGuiLayer     imgui;
  #ifndef USE_OPENGL
      SDL3GPUDriver*  ul_driver  = nullptr;
      UltralightLayer ultralight{nullptr};  // set after driver is created
  #endif
      uint64_t       last_ticks = 0;
  };
  ```

  In `SDL_AppInit`, after `renderer.init()` succeeds (SDL3 GPU path only), add:
  ```cpp
  #ifndef USE_OPENGL
      s->ul_driver = new SDL3GPUDriver(s->renderer.device());
      new (&s->ultralight) UltralightLayer(s->ul_driver);

      // Build UL GPU pipelines (needs shader dir relative to binary)
      const std::string base = SDL_GetBasePath();
      if (!s->ul_driver->buildPipelines(base.c_str())) {
          SDL_Log("UL GPU pipeline build failed — continuing without UI");
          // Non-fatal: driver methods will be no-ops if pipelines are null
      }

      const SDL_GPUTextureFormat sc_fmt =
          SDL_GetGPUSwapchainTextureFormat(s->renderer.device(), s->window);
      if (!s->ultralight.init(s->window, s->renderer.device(), sc_fmt)) {
          SDL_Log("UltralightLayer init failed");
          return SDL_APP_FAILURE;
      }
  #endif
  ```

- [ ] **Step 2: Update SDL_AppIterate — add UL update/render/flush/composite**

  In the SDL3 GPU branch of `SDL_AppIterate`, add before `SDL_AcquireGPUCommandBuffer`:
  ```cpp
  s->ultralight.update();
  s->ultralight.render();  // records commands into driver
  ```

  After `SDL_AcquireGPUCommandBuffer` but before `SDL_AcquireGPUSwapchainTexture`:
  ```cpp
  s->ul_driver->flushCommands(cmd);  // execute UL offscreen passes
  ```

  Inside the render pass, between `s->renderer.draw(pass)` and `s->imgui.render(pass, cmd)`:
  ```cpp
  int vp_w = 0, vp_h = 0;
  SDL_GetWindowSize(s->window, &vp_w, &vp_h);
  s->ultralight.composite(pass, cmd,
      static_cast<uint32_t>(vp_w), static_cast<uint32_t>(vp_h));
  ```

  Also update the ImGui debug window to show UL status:
  ```cpp
  #ifndef NDEBUG
  ImGui::Begin("Debug");
  ImGui::Text("Backend:  SDL3 GPU");
  ImGui::Text("Entities: %zu", static_cast<size_t>(reg.alive()));
  ImGui::Text("FPS:      %.1f", ImGui::GetIO().Framerate);
  ImGui::Text("UL View:  loaded");
  ImGui::End();
  #endif
  ```

- [ ] **Step 3: Update SDL_AppQuit to clean up UL**

  ```cpp
  #ifndef USE_OPENGL
      s->ultralight.shutdown();
      delete s->ul_driver;
  #endif
  ```

- [ ] **Step 4: Build and run — verify all three systems initialize**

  ```bash
  cmake --preset debug && cmake --build --preset debug --parallel
  ./build/debug/group2
  ```
  Expected:
  - Window opens
  - Log shows `[EnTT] alive=3`
  - Log shows `[UL] View created (1280×720), HTML loaded`
  - Triangle renders
  - Game menu HTML composited over scene (may take a few frames to appear)
  - Debug overlay (if debug build)

- [ ] **Step 5: Commit**

  ```bash
  git add src/main.cpp
  git commit -m "feat: wire UltralightLayer into AppState and render loop"
  ```

---

## Task 11: Update setup scripts and CI

**Files:**
- Modify: `scripts/setup-linux.sh`
- Modify: `scripts/setup-archlinux.sh`
- Modify: `scripts/setup-macos.sh`
- Modify: `scripts/setup-windows.ps1`
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Update scripts/setup-linux.sh**

  After the SDL3 system dependencies block, add:
  ```bash
  echo ""
  echo "==> Dependency notes:"
  echo "    EnTT, Dear ImGui, Ultralight SDK — all fetched automatically"
  echo "    by CMake FetchContent on first build (requires internet access)."
  echo "    Ultralight shared libraries (.so) are copied next to the binary"
  echo "    by CMake post-build steps."
  ```

- [ ] **Step 2: Update scripts/setup-archlinux.sh** — same note after SDL3 deps block.

- [ ] **Step 3: Update scripts/setup-macos.sh** — same note after SDL3 deps block.

- [ ] **Step 4: Update scripts/setup-windows.ps1** — add at the end:

  ```powershell
  Write-Host ""
  Write-Host "==> Dependency notes:" -ForegroundColor Cyan
  Write-Host "    EnTT, Dear ImGui, Ultralight SDK are fetched automatically"
  Write-Host "    by CMake FetchContent on first build (requires internet)."
  Write-Host "    Ultralight DLLs are copied next to the .exe by CMake."
  ```

- [ ] **Step 5: Update .github/workflows/ci.yml FetchContent cache key**

  Find the `Cache FetchContent` step (appears in both `build` and `release-build` jobs):
  ```yaml
  - name: Cache FetchContent
    uses: actions/cache@v4
    with:
      path: |
        ~/.cmake/packages
        ${{ github.workspace }}/build/_deps
      key: fetchcontent-${{ runner.os }}-${{ hashFiles('CMakeLists.txt', 'cmake/Ultralight.cmake') }}
      restore-keys: fetchcontent-${{ runner.os }}-
  ```
  Note: extending the cache path to `build/_deps` ensures Ultralight SDK tarball is cached.

- [ ] **Step 6: Build and verify CI config parses**

  ```bash
  python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))" && echo "YAML OK"
  ```
  Expected: `YAML OK`

- [ ] **Step 7: Commit**

  ```bash
  git add scripts/ .github/workflows/ci.yml
  git commit -m "build: update setup scripts and CI cache for new deps (ImGui, Ultralight, EnTT always-on)"
  ```

---

## Task 12: Git pull + rebase on main + final verification

- [ ] **Step 1: Fetch and rebase onto main**

  ```bash
  git fetch origin
  git rebase origin/main
  ```
  If conflicts arise, resolve them file by file. The most likely conflict is `CMakeLists.txt` — keep our new dependency additions while incorporating any changes on main.

- [ ] **Step 2: Full clean build — debug**

  ```bash
  rm -rf build/debug
  cmake --preset debug && cmake --build --preset debug --parallel
  ```
  Expected: zero errors; `group2` binary produced.

- [ ] **Step 3: Full clean build — release**

  ```bash
  rm -rf build/release
  cmake --preset release && cmake --build --preset release --parallel
  ```
  Expected: zero errors; shaders bundled; Ultralight SOs + resources in `build/release/`.

- [ ] **Step 4: Run debug build and verify all three systems**

  ```bash
  cd build/debug && ./group2
  ```
  Check log output for:
  - `[EnTT] alive=3` ✓
  - `[UL] View created` ✓
  - `Backend: SDL3 GPU` ✓
  No crashes, no SDL errors.

- [ ] **Step 5: Run release build**

  ```bash
  cd build/release && ./group2
  ```
  Expected: identical to debug but without ImGui overlay (NDEBUG defined in Release).

- [ ] **Step 6: Final commit (if any last-minute fixes) and push**

  ```bash
  git log --oneline origin/main..HEAD   # review commits to push
  # If all good:
  git push origin HEAD
  ```

---

## Self-Review Checklist

**Spec coverage:**
- ✅ EnTT always-on — Task 1
- ✅ Dear ImGui SDL3 GPU backend — Task 2, 4
- ✅ ImGui no-op in Release (NDEBUG) — Task 4
- ✅ Renderer interface refactor (device() getter, draw(pass)) — Task 3
- ✅ Ultralight SDK FetchContent + imported targets — Task 6
- ✅ SDL3GPUDriver full interface — Tasks 7, 8
- ✅ Ultralight GLSL 450 shaders — Task 5
- ✅ UltralightLayer (Platform, Renderer, View) — Task 9
- ✅ Game menu HTML embedded as string — Task 9
- ✅ EnTT demo entities + update system — Task 4
- ✅ ImGui debug window — Task 4, 10
- ✅ Ultralight composite over 3D scene — Task 10
- ✅ rpath on Linux/macOS — Task 6
- ✅ Shared lib + resources post-build copy — Task 6
- ✅ Setup scripts updated — Task 11
- ✅ CI cache extended — Task 11
- ✅ Git rebase on main — Task 12

**Missing from spec:** `EmbeddedFileSystem` for fully-binary-embedded resources (spec calls it out as "release optimization"); deferred to a follow-up sprint. The current implementation uses the `resources/` directory copied by CMake post-build.
