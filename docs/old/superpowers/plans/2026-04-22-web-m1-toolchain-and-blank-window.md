# Web Client Port — M1: Toolchain and Blank Window

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the web toolchain end-to-end: an SDL3 window created by the `klukaszek/SDL@emscripten-webgpu` fork, clearing to a solid color every frame, running inside a browser tab via Emscripten + WebGPU. No game logic, no shaders, no networking.

**Architecture:** Add a `web` CMake preset that swaps the SDL3 dependency for klukaszek's fork and skips desktop-only deps (SDL3_net, ImGui, assimp) when `EMSCRIPTEN` is defined. Create an isolated `client_web_minimal` executable built from a brand-new 80-line `minimal_main.cpp` so we validate the toolchain independently of the 3,500-line renderer. Later milestones build on this foundation without re-litigating CMake.

**Tech Stack:** CMake 3.25, Ninja, Emscripten ≥ 3.1.69, `klukaszek/SDL@emscripten-webgpu` (commit `187eb153976736f7aa5a1d8ea6c39968e35032ca`), WebGPU via the fork's `SDL_CreateGPUDevice` backend.

**Parent spec:** `docs/superpowers/specs/2026-04-22-web-client-port-design.md`

**Prereqs before starting:**
- `emsdk` installed and available via `source $EMSDK/emsdk_env.sh`. If you don't have it, see Task 2.
- Working desktop build (verify `cmake --preset debug && cmake --build --preset debug` still succeeds before touching anything).

---

### Task 1: Create a dedicated worktree

**Files:** none — this is a git-level setup task.

The user explicitly asked for implementation to happen in a new worktree so desktop work on `main` continues uninterrupted.

- [ ] **Step 1: Verify current branch is clean or commit anything pending**

Run: `git status`

Expected: working tree clean, or only the unrelated `config.toml` modification shown in prior session. If there's active work other than `config.toml`, commit or stash it first.

- [ ] **Step 2: Create the worktree**

Run:
```bash
git worktree add ../group2-web-port -b web-port
```

Expected: `Preparing worktree (new branch 'web-port') ... HEAD is now at <sha> ...`.

- [ ] **Step 3: Switch to the worktree directory for all subsequent tasks**

Run:
```bash
cd ../group2-web-port
pwd        # should print /home/user/Documents/dev/group2-web-port
git branch # should show "* web-port"
```

- [ ] **Step 4: Copy the spec and plan into the worktree (they're gitignored in the repo)**

Run:
```bash
mkdir -p docs/superpowers/specs docs/superpowers/plans
cp ../group2/docs/superpowers/specs/2026-04-22-web-client-port-design.md docs/superpowers/specs/
cp ../group2/docs/superpowers/plans/2026-04-22-web-m1-toolchain-and-blank-window.md docs/superpowers/plans/
```

No commit — `docs/superpowers/` is gitignored.

- [ ] **Step 5: Verify the desktop build still works from the worktree**

Run:
```bash
cmake --preset debug
cmake --build --preset debug -j
```

Expected: builds successfully. This is our baseline — anything that breaks the desktop build in later tasks is a regression.

---

### Task 2: Install emsdk (manual prerequisite)

**Files:** none — this is a host machine setup task.

Skip this task if `emcc --version` already reports `>= 3.1.69`.

- [ ] **Step 1: Clone emsdk**

Run:
```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk
```

- [ ] **Step 2: Install and activate a known-good version**

Run:
```bash
./emsdk install 3.1.74
./emsdk activate 3.1.74
```

Expected: version `3.1.74` installed and activated. Any `>= 3.1.69` works; `3.1.74` is a well-tested baseline.

- [ ] **Step 3: Source the env script and verify**

Run:
```bash
source ~/emsdk/emsdk_env.sh
emcc --version
```

Expected: prints `emcc (Emscripten gcc/clang-like replacement) 3.1.74 ...`.

- [ ] **Step 4: Return to the worktree and keep the env sourced in this shell**

Run:
```bash
cd ~/Documents/dev/group2-web-port
echo $EMSDK     # should print ~/emsdk
which emcc      # should print ~/emsdk/upstream/emscripten/emcc
```

Note: every new terminal needs `source ~/emsdk/emsdk_env.sh` before running `cmake --preset web`. Task 11 creates a helper script.

---

### Task 3: Add an Emscripten-aware CMake toolchain wrapper

**Files:**
- Modify: `cmake/toolchains/auto.cmake`

The existing `auto.cmake` assumes native clang. When `CMAKE_SYSTEM_NAME=Emscripten` (set by Emscripten's own toolchain), we defer to Emscripten's toolchain and do nothing native-specific.

- [ ] **Step 1: Read the current toolchain file**

Run: read `cmake/toolchains/auto.cmake`.

- [ ] **Step 2: Add an Emscripten guard at the top**

Edit `cmake/toolchains/auto.cmake` — replace the entire platform cascade with:

```cmake
# cmake/toolchains/auto.cmake
# Auto-detect the right compiler for the host platform so a single set of
# CMake presets works on Linux, macOS, and Windows — no -mac/-win suffixes.
#
#   Linux       → clang / clang++ from PATH
#   macOS       → /usr/bin/clang  (Apple Clang — avoids Homebrew LLVM in PATH)
#   Windows     → MSVC via cmake/toolchains/msvc.cmake
#   Emscripten  → no-op here; Emscripten.cmake is the real toolchain file and
#                 is chained in by the `web` preset via CMAKE_TOOLCHAIN_FILE.

cmake_minimum_required(VERSION 3.25)

if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Emscripten's own toolchain file (Emscripten.cmake) already set the
    # compiler (emcc/em++). Don't override it.
    return()
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    include("${CMAKE_CURRENT_LIST_DIR}/msvc.cmake")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    # The setup-macos.sh script adds Homebrew LLVM 18 to PATH for
    # clang-format-18, which shadows the system clang.  Homebrew's clang
    # cannot find macOS SDK C headers (<stddef.h>, etc.), so we must use
    # Apple Clang via absolute path.
    set(CMAKE_C_COMPILER   /usr/bin/clang)
    set(CMAKE_CXX_COMPILER /usr/bin/clang++)
else()
    set(CMAKE_C_COMPILER   clang)
    set(CMAKE_CXX_COMPILER clang++)
endif()
```

- [ ] **Step 3: Verify the desktop build still works**

Run:
```bash
cmake --preset debug
cmake --build --preset debug -j --target client
```

Expected: builds successfully. No behavior change for desktop.

- [ ] **Step 4: Commit**

Run:
```bash
git add cmake/toolchains/auto.cmake
git commit -m "cmake: make auto toolchain Emscripten-aware

Return early when CMAKE_SYSTEM_NAME=Emscripten so the fork's
Emscripten.cmake stays in charge of compiler + flags. No-op for
existing desktop presets."
```

---

### Task 4: Add the `web` CMake preset

**Files:**
- Modify: `CMakePresets.json`

The preset chains Emscripten's toolchain file as `CMAKE_TOOLCHAIN_FILE`, inherits the project's `base` preset (so `ccache`, `FETCHCONTENT_BASE_DIR`, etc. still work), and sets a flag `GROUP2_WEB_CLIENT=ON` that the CMakeLists uses in later tasks as the canonical "are we building the web target" switch.

- [ ] **Step 1: Read the current CMakePresets.json to find the right insertion point**

Run: read `CMakePresets.json`.

- [ ] **Step 2: Add the configure preset**

In `CMakePresets.json`, inside `"configurePresets": [ ... ]`, append a new entry **after** the existing `relwithdebinfo` entry and **before** the closing `]`:

```json
,
    {
      "name": "web",
      "displayName": "Web (Emscripten + WebGPU)",
      "inherits": "base",
      "toolchainFile": "$env{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "GROUP2_WEB_CLIENT": "ON"
      },
      "environment": {
        "EMSDK_QUIET": "1"
      }
    }
```

Note: `"inherits": "base"` pulls in the `auto.cmake` toolchain via `base`'s `toolchainFile`. The explicit `toolchainFile` here **overrides** it with Emscripten's, which is what we want. CMake chains toolchains correctly when `toolchainFile` is overridden at a deeper preset layer.

- [ ] **Step 3: Add the matching build preset**

In the same file, inside `"buildPresets": [ ... ]`, append:

```json
,
    { "name": "web", "configurePreset": "web" }
```

- [ ] **Step 4: Syntax check the JSON**

Run:
```bash
python3 -c "import json; json.load(open('CMakePresets.json'))" && echo "JSON OK"
```

Expected: `JSON OK`.

- [ ] **Step 5: Dry-run the new preset (configure will fail at SDL3; that's expected)**

Run:
```bash
source ~/emsdk/emsdk_env.sh
cmake --preset web
```

Expected: CMake starts, reports "The CXX compiler identification is Emscripten ...", then **fails** later in the `SDL3` FetchContent step because Emscripten can't build mainline SDL3's full dep set. That's the signal the toolchain swap is working — we fix the SDL3 dep in Task 5.

- [ ] **Step 6: Commit**

Run:
```bash
git add CMakePresets.json
git commit -m "cmake: add web preset targeting Emscripten

Adds \`web\` configure + build presets that chain Emscripten's toolchain
on top of the \`base\` preset. Sets GROUP2_WEB_CLIENT=ON so CMakeLists
branches can gate web-only logic on that variable (in addition to the
built-in EMSCRIPTEN variable)."
```

---

### Task 5: Swap SDL3 for the fork when building for web

**Files:**
- Modify: `CMakeLists.txt` (around line 76-90 — the existing SDL3 FetchContent block)

Pin SHA per the spec's decision table row 4. No fork of our own yet; we fork only if klukaszek's branch churns.

- [ ] **Step 1: Read the current SDL3 FetchContent block**

Run: read `CMakeLists.txt` lines 70–95.

- [ ] **Step 2: Replace the SDL3 FetchContent_Declare with a platform-branched version**

Find the existing block (starts with `# SDL3 — window, input, GPU pipeline ...`) and replace the `FetchContent_Declare(SDL3 ...)` call (preserving any `FetchContent_MakeAvailable(SDL3)` call that follows) with:

```cmake
# SDL3 — window, input, GPU pipeline (Vulkan/Metal/DX12 on desktop; WebGPU on web)
#
# On desktop we use mainline SDL3 release 3.2.x (same as before).
# On web (EMSCRIPTEN) we use klukaszek's SDL fork on the `emscripten-webgpu`
# branch — mainline SDL3 has no WebGPU GPU backend as of April 2026. The fork
# is pinned to a specific commit SHA for reproducibility; see
# docs/superpowers/specs/2026-04-22-web-client-port-design.md §7.
if(EMSCRIPTEN)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/klukaszek/SDL.git
        GIT_TAG        187eb153976736f7aa5a1d8ea6c39968e35032ca  # emscripten-webgpu tip
        GIT_SHALLOW    FALSE  # SHA-pin + shallow is unreliable
    )
else()
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.2.0
    )
endif()
```

(If the existing declaration uses a different `GIT_TAG` than `release-3.2.0`, preserve that exact tag in the `else()` branch — do not "upgrade" the desktop SDL3 version as part of this plan.)

- [ ] **Step 3: Also branch the `SDL_SHARED`/`SDL_STATIC` vars if the existing block sets them**

If the existing block has lines like `set(SDL_SHARED OFF CACHE BOOL ...)` / `set(SDL_PIPEWIRE OFF CACHE BOOL ...)`, keep them as they are — they apply to both desktop and the fork.

- [ ] **Step 4: Verify desktop build is unaffected**

Run:
```bash
cmake --preset debug
cmake --build --preset debug -j --target client
```

Expected: builds successfully; same SDL3 mainline as before.

- [ ] **Step 5: Commit**

Run:
```bash
git add CMakeLists.txt
git commit -m "cmake: branch SDL3 source by platform — fork on Emscripten

Desktop keeps mainline SDL3 release-3.2.0. Emscripten builds pull
klukaszek/SDL branch emscripten-webgpu pinned to SHA 187eb15 for the
WebGPU GPU backend. Zero effect on existing desktop builds."
```

---

### Task 6: Gate out desktop-only deps on Emscripten

**Files:**
- Modify: `CMakeLists.txt` (SDL3_net block ~L91-100, Assimp block ~L130-158, ImGui block ~L217-245)

Wrap three FetchContent blocks in `if(NOT EMSCRIPTEN)`. M1 doesn't need any of them for the minimal target; later milestones will either port them in or keep them excluded.

- [ ] **Step 1: Read the three blocks**

Find and read the blocks for SDL3_net, assimp, and ImGui (including `imgui_lib` `add_library` target that follows the FetchContent).

- [ ] **Step 2: Wrap each block in `if(NOT EMSCRIPTEN) ... endif()`**

For SDL3_net (around L91-100):
```cmake
# SDL3_net — networking library for SDL3 (desktop only; browser uses WebSocket
# via a separate ITransport implementation — see spec §10).
if(NOT EMSCRIPTEN)
    FetchContent_Declare(
        SDL3_net
        GIT_REPOSITORY https://github.com/cedricdvd/SDL_net.git
        # ... existing GIT_TAG and other args unchanged ...
    )
    FetchContent_MakeAvailable(SDL3_net)
endif()
```

For assimp (around L130-158) — same pattern: wrap the entire `FetchContent_Declare(assimp ...)` + `FetchContent_MakeAvailable(assimp)` + any post-population includes in `if(NOT EMSCRIPTEN) ... endif()`. M2 revisits this decision.

For ImGui (around L217-245) — wrap the `FetchContent_Declare(imgui ...)`, the `FetchContent_MakeAvailable(imgui)`, **and** the `add_library(imgui_lib STATIC ...)` + all `target_include_directories`/`target_link_libraries`/`target_compile_options` calls operating on `imgui_lib` in the same `if(NOT EMSCRIPTEN) ... endif()` block.

- [ ] **Step 3: Verify desktop build is unaffected**

Run:
```bash
cmake --preset debug
cmake --build --preset debug -j --target client
```

Expected: builds successfully. Desktop should link SDL3_net, assimp, imgui_lib as before.

- [ ] **Step 4: Re-run the web configure (will fail at `client` target due to missing imgui/SDL3_net linkage; that's expected for M1)**

Run:
```bash
source ~/emsdk/emsdk_env.sh
cmake --preset web
```

Expected: configure succeeds this time (SDL fork downloads + builds; assimp/SDL3_net/imgui skipped); any configure-step error unrelated to the `client` or `group2` targets is a regression to fix now.

If the configure now tries to build the existing `client` target and fails because it references `SDL3_net::SDL3_net` or `imgui_lib`, that's normal — Task 7 solves it by introducing a separate minimal target and later marking the `client`/`group2`/`server` targets as desktop-only.

- [ ] **Step 5: Commit**

Run:
```bash
git add CMakeLists.txt
git commit -m "cmake: skip SDL3_net, assimp, imgui on Emscripten

These deps are either desktop-only (SDL3_net — browser uses WebSocket)
or deferred to a later web milestone (assimp, imgui). Wrapping their
FetchContent + imgui_lib target declarations in if(NOT EMSCRIPTEN)
keeps web configure times short and dependency surface minimal for
the M1 toolchain smoke test."
```

---

### Task 7: Mark existing client/server/group2 targets desktop-only

**Files:**
- Modify: `CMakeLists.txt` (around L467 `add_executable(group2 ...)`, L692 `add_executable(client ...)`, L900 `add_executable(server ...)`)

Wrap the three existing `add_executable` blocks (and their associated `target_link_libraries`, `target_include_directories`, `target_precompile_headers`, `target_compile_definitions`, and `add_custom_command` calls that reference those targets) in `if(NOT EMSCRIPTEN) ... endif()`.

- [ ] **Step 1: Find each target's full block extent**

For each of `group2`, `client`, `server`, identify the line where `add_executable` starts and the last line that references that target (scroll down until you hit the next `add_executable` or end-of-file). Include every `target_*()`, `add_custom_command(TARGET ...)`, and `set_target_properties(...)` call for that target.

- [ ] **Step 2: Wrap each block**

For the `group2` target (L467-onwards until the next `add_executable`), add:
```cmake
if(NOT EMSCRIPTEN)
    add_executable(group2
        # ... existing sources unchanged ...
    )
    # ... all existing target_*() and custom commands for group2 unchanged ...
endif()
```

Do the same for `client` (L692-onwards) and `server` (L900-onwards). Each `if(NOT EMSCRIPTEN)` wraps only that single target and its config.

**Do NOT nest** — three separate `if(NOT EMSCRIPTEN) ... endif()` blocks, one per target.

- [ ] **Step 3: Verify desktop build is unaffected**

Run:
```bash
cmake --preset debug
cmake --build --preset debug -j --target group2 --target client --target server
```

Expected: all three targets build successfully.

- [ ] **Step 4: Verify web configure no longer attempts to build desktop targets**

Run:
```bash
source ~/emsdk/emsdk_env.sh
rm -rf build/web .deps/web
cmake --preset web
```

Expected: configure succeeds; no targets to build yet (that's fine — Task 9 adds `client_web_minimal`).

- [ ] **Step 5: Commit**

Run:
```bash
git add CMakeLists.txt
git commit -m "cmake: gate group2/client/server targets as desktop-only

Each add_executable + its target_*() configuration is now wrapped in
if(NOT EMSCRIPTEN) so web configure skips them cleanly. The web build
adds its own targets (client_web_minimal in M1) rather than trying to
repurpose the desktop ones — keeps blast radius small while the port
is experimental."
```

---

### Task 8: Write the minimal web entry point

**Files:**
- Create: `src/client/web/minimal_main.cpp`

80 lines of SDL3: open a window, create a GPU device, clear to a solid color each frame, handle quit. Nothing else. This is our "does the toolchain work" smoke test.

- [ ] **Step 1: Create the source file**

Create `src/client/web/minimal_main.cpp`:

```cpp
/// @file minimal_main.cpp
/// @brief M1 web smoke test — open an SDL window, create a GPU device via the
/// klukaszek/SDL fork's WebGPU backend, clear to a solid color every frame.
///
/// This file exists ONLY for the M1 milestone of the web port. It is replaced
/// by the full client entry (main.cpp + Game.cpp) in later milestones.

#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_main.h>

namespace {

struct AppState
{
    SDL_Window*    window = nullptr;
    SDL_GPUDevice* device = nullptr;
    Uint64         startTicks = 0;
};

} // namespace

extern "C" SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* /*argv*/[])
{
    auto* state = new AppState();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->window = SDL_CreateWindow("group2 — web M1 smoke test",
                                     1280,
                                     720,
                                     SDL_WINDOW_RESIZABLE);
    if (!state->window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // The fork's WebGPU backend advertises SDL_GPU_SHADERFORMAT_WGSL. We ask
    // for SPIRV too so the same call works unchanged on desktop — later tasks
    // will adapt this based on the shader format probe.
    constexpr SDL_GPUShaderFormat k_fmts =
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_WGSL;
    state->device = SDL_CreateGPUDevice(k_fmts, /*debug=*/true, /*name=*/nullptr);
    if (!state->device) {
        SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("GPU driver = %s", SDL_GetGPUDeviceDriver(state->device));
    SDL_Log("available shader formats = 0x%x",
            SDL_GetGPUShaderFormats(state->device));

    if (!SDL_ClaimWindowForGPUDevice(state->device, state->window)) {
        SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->startTicks = SDL_GetTicks();
    *appstate = state;
    return SDL_APP_CONTINUE;
}

extern "C" SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

extern "C" SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto* state = static_cast<AppState*>(appstate);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(state->device);
    if (!cmd)
        return SDL_APP_CONTINUE;

    SDL_GPUTexture* swap = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, state->window, &swap, &w, &h)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return SDL_APP_CONTINUE;
    }
    if (!swap) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return SDL_APP_CONTINUE;
    }

    // Cycle a color so we can visually confirm the frame loop is running.
    const float t = (float)(SDL_GetTicks() - state->startTicks) * 0.001f;
    SDL_GPUColorTargetInfo color = {};
    color.texture     = swap;
    color.clear_color = {0.1f + 0.1f * SDL_sinf(t),
                         0.2f,
                         0.4f + 0.1f * SDL_cosf(t),
                         1.0f};
    color.load_op     = SDL_GPU_LOADOP_CLEAR;
    color.store_op    = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    return SDL_APP_CONTINUE;
}

extern "C" void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    auto* state = static_cast<AppState*>(appstate);
    if (!state) return;
    if (state->device) {
        if (state->window)
            SDL_ReleaseWindowFromGPUDevice(state->device, state->window);
        SDL_DestroyGPUDevice(state->device);
    }
    if (state->window)
        SDL_DestroyWindow(state->window);
    SDL_Quit();
    delete state;
}
```

- [ ] **Step 2: Commit**

Run:
```bash
git add src/client/web/minimal_main.cpp
git commit -m "web(m1): minimal SDL3 GPU smoke test entry point

80-line SDL_MAIN_USE_CALLBACKS app that creates a window, claims a
GPU device via the fork's WebGPU backend, and clears to an animated
color each frame. Exists only for the M1 toolchain proof-of-life;
replaced by the full Game entry in M3."
```

---

### Task 9: Add the `client_web_minimal` CMake target

**Files:**
- Modify: `CMakeLists.txt` (new block appended near the bottom, after the desktop target blocks)

- [ ] **Step 1: Append a new `if(EMSCRIPTEN)` block to CMakeLists.txt**

Add the following at the end of `CMakeLists.txt` (after the last existing `endif()` that closes Task 7's server wrap):

```cmake
# -----------------------------------------------------------------------------
# Web client (M1 minimal)
# -----------------------------------------------------------------------------
# This target is built only under Emscripten. It is the M1 "does the toolchain
# work" smoke test — open a window via the SDL fork's WebGPU backend and clear
# to a solid color. Later milestones add shaders (M2), renderer (M3), and
# networking (M4), eventually replacing this target with the full client.
# -----------------------------------------------------------------------------
if(EMSCRIPTEN)
    # Require modern emcc for the fork's WebGPU backend.
    if(EMSCRIPTEN_VERSION VERSION_LESS "3.1.69")
        message(FATAL_ERROR
            "Web build requires emcc >= 3.1.69 (got ${EMSCRIPTEN_VERSION}). "
            "Run `~/emsdk/emsdk install 3.1.74 && ~/emsdk/emsdk activate 3.1.74`.")
    endif()

    add_executable(client_web_minimal
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/web/minimal_main.cpp"
    )

    target_link_libraries(client_web_minimal PRIVATE
        SDL3::SDL3-static
    )

    # Emscripten link flags for the WebGPU backend. Per spec §6.2 and the
    # klukaszek/SDL3-WebGPU-Examples CMakeLists.
    target_link_options(client_web_minimal PRIVATE
        "SHELL:-sUSE_WEBGPU=1"
        "SHELL:-sASYNCIFY=1"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sINITIAL_MEMORY=256MB"
        "SHELL:-sSTACK_SIZE=1MB"
        "SHELL:-sMODULARIZE=1"
        "SHELL:-sEXPORT_ES6=1"
        "SHELL:-sEXPORT_NAME=createGame"
        "SHELL:-sENVIRONMENT=web"
        "SHELL:-sDISABLE_EXCEPTION_CATCHING=0"
        "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    )

    # Emscripten names the executable by the suffix we pick — .html packages
    # up the HTML shell + JS loader + wasm + (optional) data file.
    set_target_properties(client_web_minimal PROPERTIES
        SUFFIX ".html"
    )
endif()
```

- [ ] **Step 2: Reconfigure and attempt the build — expect a shell.html missing error**

Run:
```bash
source ~/emsdk/emsdk_env.sh
rm -rf build/web .deps/web
cmake --preset web
cmake --build --preset web --target client_web_minimal
```

Expected: configure succeeds, build fails at the link step with `--shell-file: file not found: web/shell.html`. That's the next task. (If it fails earlier at a different step — read the error and fix.)

- [ ] **Step 3: Do NOT commit yet — wait for Task 10 so the build is green**

---

### Task 10: Create the minimal HTML shell

**Files:**
- Create: `web/shell.html`

Minimal — just the canvas and a tiny status area. Fancier shell (progress bar, click-to-play, caps panel) arrives in M5.

- [ ] **Step 1: Create the shell**

Create `web/shell.html`:

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>group2 — web M1</title>
<style>
  html, body { margin: 0; padding: 0; height: 100%; background: #111; color: #ddd;
               font: 13px/1.4 system-ui, sans-serif; }
  #app { display: flex; flex-direction: column; height: 100%; }
  #canvas { flex: 1; width: 100%; display: block; outline: none; }
  #status { padding: 6px 10px; background: #000; border-top: 1px solid #333;
            font-family: ui-monospace, monospace; white-space: pre-wrap; }
</style>
</head>
<body>
<div id="app">
  <canvas id="canvas" tabindex="0"></canvas>
  <div id="status">Loading …</div>
</div>
<script type="module">
  const statusEl = document.getElementById('status');
  const canvas   = document.getElementById('canvas');

  const setStatus = msg => { statusEl.textContent = msg; };

  window.Module = {
    canvas,
    print:     t => { console.log(t); setStatus(t); },
    printErr:  t => { console.error(t); setStatus('ERR: ' + t); },
    setStatus,
    onRuntimeInitialized: () => setStatus('Running.'),
  };

  // Emscripten-generated JS module (EXPORT_NAME=createGame + MODULARIZE=1).
  const { default: createGame } = await import('./client_web_minimal.js');
  createGame(window.Module).catch(err => {
    console.error(err);
    setStatus('FATAL: ' + err.message);
  });
</script>
</body>
</html>
```

- [ ] **Step 2: Rebuild — expect success now**

Run:
```bash
cmake --build --preset web --target client_web_minimal
```

Expected: build succeeds. `build/web/` contains `client_web_minimal.html`, `client_web_minimal.js`, `client_web_minimal.wasm`.

- [ ] **Step 3: Commit shell + target together**

Run:
```bash
git add web/shell.html CMakeLists.txt
git commit -m "web(m1): add client_web_minimal target + minimal HTML shell

client_web_minimal is an Emscripten-only executable producing
client_web_minimal.html/.js/.wasm for the M1 toolchain proof. Uses the
SDL fork's WebGPU backend via -sUSE_WEBGPU + -sASYNCIFY. The HTML shell
is deliberately tiny (canvas + status line) — richer UX arrives in M5."
```

---

### Task 11: Create the dev-loop helper script

**Files:**
- Create: `scripts/dev-web.sh`

One-shot script: source emsdk, configure, build, serve via `emrun`. Makes the dev loop `./scripts/dev-web.sh` + browser refresh.

- [ ] **Step 1: Create the script**

Create `scripts/dev-web.sh`:

```bash
#!/usr/bin/env bash
# scripts/dev-web.sh — build the web client and serve it locally.
#
# Usage:   ./scripts/dev-web.sh [--port 8000]
# Effect:  sources emsdk from $EMSDK (default ~/emsdk), configures/builds the
#          `web` preset, then runs emrun with no_browser so you can open the
#          URL yourself.

set -euo pipefail

EMSDK_DIR="${EMSDK:-$HOME/emsdk}"
PORT=8000

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

if [[ ! -f "$EMSDK_DIR/emsdk_env.sh" ]]; then
    echo "ERROR: emsdk not found at $EMSDK_DIR" >&2
    echo "       install with: git clone https://github.com/emscripten-core/emsdk.git $EMSDK_DIR" >&2
    exit 1
fi

# shellcheck disable=SC1091
source "$EMSDK_DIR/emsdk_env.sh"

cmake --preset web
cmake --build --preset web --target client_web_minimal

echo ""
echo "Build complete. Opening emrun on :$PORT …"
echo "URL: http://localhost:$PORT/client_web_minimal.html"
echo ""

exec emrun \
    --no_browser \
    --port "$PORT" \
    --serve_after_exit \
    build/web/client_web_minimal.html
```

- [ ] **Step 2: Make it executable**

Run:
```bash
chmod +x scripts/dev-web.sh
```

- [ ] **Step 3: Run it**

Run (in a fresh terminal, or with `emsdk_env.sh` not yet sourced):
```bash
./scripts/dev-web.sh
```

Expected:
- emsdk env loads
- cmake reconfigures if needed (no-op if already configured)
- cmake builds `client_web_minimal` (incremental — fast)
- `emrun` starts serving on `http://localhost:8000/client_web_minimal.html`
- Terminal prints `Web server started at http://localhost:8000/ ...`

- [ ] **Step 4: Open the URL in a WebGPU-capable browser (Chrome or Edge ≥ 113)**

Open: `http://localhost:8000/client_web_minimal.html`

Expected:
- Page shows status line "Loading …" briefly, then "Running."
- A 1280×720 canvas renders an animated color cycling between `(0.0–0.2, 0.2, 0.3–0.5, 1.0)` RGB.
- Browser DevTools console prints:
  - `GPU driver = webgpu` (or similar fork-reported name)
  - `available shader formats = 0x8` (SDL_GPU_SHADERFORMAT_WGSL = 0x8, per SDL3's header)

If the canvas stays black and the console shows `SDL_CreateGPUDevice failed`, the most likely cause is an unsupported browser — verify with `chrome://gpu` that WebGPU is reported as "Hardware accelerated". If it's disabled, enable `chrome://flags/#enable-unsafe-webgpu`.

- [ ] **Step 5: Commit the dev script**

Run:
```bash
git add scripts/dev-web.sh
git commit -m "web(m1): add scripts/dev-web.sh one-shot dev loop

Sources emsdk, configures/builds the web preset, and serves via emrun.
Default port 8000, overridable with --port. Exists so the daily dev
loop is a single command instead of four."
```

---

### Task 12: Document the web build in the repo README

**Files:**
- Modify: `README.md`

Add a short "Web (experimental)" section noting M1 status. Later milestones extend it.

- [ ] **Step 1: Read the current README sections**

Run: read `README.md` — find the end of the existing "Prerequisites" section or similar.

- [ ] **Step 2: Add a new section after the existing Linux/macOS/Windows setup blocks**

Append near the setup sections:

```markdown
### Web (experimental — M1 milestone)

Browser build using Emscripten + WebGPU via the
[klukaszek/SDL](https://github.com/klukaszek/SDL) fork
(branch `emscripten-webgpu`, pinned SHA). Current M1 status: toolchain proof
of life — opens a window and renders a cycling clear color. No game logic yet.

```bash
# one-time install
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 3.1.74
~/emsdk/emsdk activate 3.1.74

# build + serve
./scripts/dev-web.sh
# then open http://localhost:8000/client_web_minimal.html in Chrome/Edge ≥ 113
```

Design + milestone roadmap: see `docs/superpowers/specs/2026-04-22-web-client-port-design.md`
(local; path is gitignored) or ask Claude to regenerate from the brainstorming
transcript.
```

- [ ] **Step 3: Commit**

Run:
```bash
git add README.md
git commit -m "docs: document the M1 web build in the README

Short experimental-section entry pointing at scripts/dev-web.sh and
flagging the current milestone status. Expands in later milestones as
features land."
```

---

### Task 13: End-to-end smoke verification

**Files:** none — this is a verification task.

Clean-build from scratch and confirm both platforms still work. If this fails, an earlier task regressed something.

- [ ] **Step 1: Clean build the desktop client**

Run:
```bash
rm -rf build/debug .deps/debug
cmake --preset debug
cmake --build --preset debug -j --target client --target server --target group2
```

Expected: all three targets build successfully.

- [ ] **Step 2: Clean build the web client**

Run:
```bash
rm -rf build/web .deps/web
source ~/emsdk/emsdk_env.sh
cmake --preset web
cmake --build --preset web -j --target client_web_minimal
```

Expected: build succeeds; `build/web/client_web_minimal.{html,js,wasm}` exist.

- [ ] **Step 3: Launch the web client, confirm visible behavior**

Run:
```bash
./scripts/dev-web.sh
```

Open `http://localhost:8000/client_web_minimal.html` in Chrome/Edge.

Expected:
- Status line progresses `Loading …` → `Running.`
- Canvas shows an animated cyan-to-blue color cycle.
- DevTools console: no errors; prints `GPU driver = ...` and `available shader formats = 0x...`.

- [ ] **Step 4: Record any anomalies in an M1 follow-ups note**

If any of the following happened, note them in `docs/superpowers/plans/M1-FOLLOWUPS.md` (gitignored) for M2 planning:
- Shader formats bitmask did NOT include `0x8` (WGSL = bit 3 per SDL3 `SDL_gpu.h`).
- Emscripten version differs from expected (e.g., emsdk upgraded during the work).
- Any warnings from the SDL fork's CMake that look relevant.
- Browser console errors beyond our own `SDL_Log` output.

- [ ] **Step 5: Mark M1 complete**

Run:
```bash
echo "M1 verified $(date -Iseconds)" >> docs/superpowers/plans/M1-FOLLOWUPS.md
git log --oneline "origin/main..HEAD" | head -20
```

Inspect: the log should show the 12 commits made in this plan (one per task, except Task 10 which includes two files). If any task was skipped or batched differently, that's fine — content matters, not count.

---

## What's next

M1 proves the toolchain. Next step is **M2: shader pipeline** — wire up `SDL_gpu_shadercross` + Tint, render one model with `geometry.vert/frag` through the WebGPU backend. That plan gets written once this one is executed and any M1 findings are incorporated.

Do not proceed directly to M2 from this plan — we write M2 freshly after M1 is green so the plan can absorb real findings (actual shader-format bitmask value, any stability issues we hit, concrete emcc warnings).
