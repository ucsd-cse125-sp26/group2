# group2

CSE 125 Spring 2026 — Quake-style FPS in C++23, SDL3 GPU, ECS.

```bash
# Linux
bash scripts/setup-linux.sh
cmake --preset debug && cmake --build --preset debug
LSAN_OPTIONS=suppressions=sanitizers/lsan.supp ./build/debug/group2

# macOS
bash scripts/setup-macos.sh
cmake --preset debug && cmake --build --preset debug
./build/debug/group2

# Windows (run setup-windows.ps1 first from an elevated PowerShell)
cmake --preset debug-win && cmake --build --preset debug-win
.\build\debug-win\group2.exe
```

## Tech stack

| Concern | Library |
|---|---|
| Window / Input | [SDL3](https://github.com/libsdl-org/SDL) |
| GPU rendering | SDL3 GPU API — Vulkan (Linux/Windows) · Metal (macOS) · Direct3D 12 (Windows) |
| Shaders | GLSL → SPIR-V (via glslc/glslangValidator) · SPIR-V → MSL (via spirv-cross, for Metal) |
| Networking | [SDL3_net](https://github.com/libsdl-org/SDL_net) |
| ECS (optional) | [EnTT](https://github.com/skypjack/entt) (`-DUSE_ENTT=ON`) or roll your own |
| Math | [GLM](https://github.com/g-truc/glm) |
| Build | CMake 3.25+ · Ninja · MSVC 2022 (Windows) · Clang (Linux/macOS) |
| Sanitizers | ASan + UBSan (debug Linux/macOS), ASan (Windows MSVC) |
| Lint | clang-format-18 · clang-tidy |
| CI | GitHub Actions (Ubuntu · macOS · Windows) |

All C++ dependencies are fetched automatically via CMake `FetchContent` — no manual library installs needed.

---

## Prerequisites

Each setup script installs build tools, the GLSL→SPIR-V shader compiler, and SDL3's
system-level dependencies in one shot. Run it once after cloning.

> **Note on spirv-cross:** `spirv-cross` **is required** on macOS and is included in the
> Vulkan SDK on Windows. SDL3's Metal backend only accepts MSL or precompiled Metal libraries —
> it does not perform any SPIR-V→MSL conversion internally.

### Linux — Debian / Ubuntu

```bash
bash scripts/setup-linux.sh
```

Installs: `cmake`, `ninja`, `clang`, `clang-format-18`, `clang-tidy-18`, `glslang-tools` (GLSL→SPIR-V), `spirv-cross` (SPIR-V→MSL), and all SDL3 system headers (X11, Wayland, ALSA, Pulse, etc.).

### Linux — Arch Linux (and derivatives: Manjaro, EndeavourOS, CachyOS…)

```bash
bash scripts/setup-archlinux.sh
```

Installs: `cmake`, `ninja`, `clang`, `shaderc` (GLSL→SPIR-V via `glslc`), `spirv-cross` (SPIR-V→MSL), and SDL3 system dependencies.

### macOS

```bash
bash scripts/setup-macos.sh
```

Requires Xcode Command Line Tools (provides clang + Metal) + [Homebrew](https://brew.sh).
Installs: `cmake`, `ninja`, `llvm@18`, `glslang` (GLSL→SPIR-V), `spirv-cross` (SPIR-V→MSL, required for Metal).

### Windows

Run from an **elevated PowerShell**:

```powershell
Set-ExecutionPolicy Bypass -Scope Process -Force
.\scripts\setup-windows.ps1
```

Installs via `winget`: **VS Build Tools 2022** (MSVC + Windows SDK), CMake, Ninja, LLVM (clang-format/clang-tidy), and the **Vulkan SDK** (provides `glslc` for GLSL→SPIR-V and `spirv-cross` for SPIR-V→MSL).

> **Note — git tag fetch:** The setup scripts run `git config --add remote.origin.fetch "+refs/tags/*:refs/tags/*"` to prevent `git pull` from failing with "would clobber existing tag". If you cloned before running a setup script, run that one line manually.

---

## Building

All commands run from the repo root.

### Linux / macOS

```bash
# Debug — AddressSanitizer + UBSan
cmake --preset debug
cmake --build --preset debug

# Release
cmake --preset release
cmake --build --preset release
```

The binary lands in `build/<preset>/group2`. Compiled SPIR-V shaders are automatically
copied to `build/<preset>/shaders/` by the CMake build.

### Windows

```powershell
cmake --preset debug-win
cmake --build --preset debug-win
```

The binary lands in `build\debug-win\group2.exe`. No Developer PowerShell needed — the CMake toolchain auto-detects MSVC via `vswhere`.

---

## IDE setup

### CLion
Open the **repo root folder** in CLion. It reads `CMakePresets.json` automatically.
The preset profiles are pre-enabled via `.idea/` files committed in this repo
and should appear already checked in **Settings > Build, Execution, Deployment > CMake**.

If CLion has added its own "Debug" profile (pointing at `cmake-build-debug/`), delete it and keep only the preset-based ones.

> **Note:** CLion modifies `workspace.xml` locally as you work. Do **not** commit those changes.

### VS Code
1. Install the recommended extensions when prompted (`.vscode/extensions.json`).
2. CMake Tools detects `CMakePresets.json` automatically.
3. Select a preset from the status bar — `debug` on Linux/macOS, `debug-win` on Windows.
4. **Build:** `Ctrl+Shift+B`.
5. **Debug:** `F5` → pick **Launch group2 (Linux / macOS)** or **Launch group2 (Windows)**.

### Visual Studio 2022
Use **File › Open › Folder** to open the repo root. VS 2022 reads `CMakePresets.json` natively.

1. Select **debug-win** from the configuration dropdown.
2. **Build:** `Ctrl+Shift+B`.
3. **Run / Debug:** `F5`.

---

## Running

```bash
./build/debug/group2      # Linux / macOS
.\build\debug-win\group2  # Windows
```

Press **Escape** or close the window to quit.

**LSan false positives on Linux:** SDL3's Linux backends (dbus, Wayland) make intentional
one-time allocations that ASan flags as leaks. Suppress them with:

```bash
LSAN_OPTIONS=suppressions=sanitizers/lsan.supp ./build/debug/group2
```

### Running Server

```bash
./build/debug/server      # Linux / macOS
.\build\debug-win\server  # Windows
```

---

## CMake options

| Option | Default | Description |
|---|---|---|
| `USE_ENTT` | `OFF` | Use EnTT ECS library; `OFF` = minimal stub in `src/ecs/Registry.hpp` |
| `ENABLE_ASAN` | `OFF` | AddressSanitizer (on by default in `debug` preset) |
| `ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer (on by default in `debug` preset) |
| `ENABLE_TSAN` | `OFF` | ThreadSanitizer (mutually exclusive with ASan) |

```bash
cmake --preset debug -DUSE_ENTT=ON
```

---

## Rendering

The renderer (`src/renderer/Renderer`) wraps **SDL3's GPU API** — a thin, cross-platform
abstraction over Vulkan (Linux/Windows), Metal (macOS), and Direct3D 12 (Windows).
Unlike SDL3's 2D `SDL_Renderer`, the GPU API gives full control over pipelines, vertex
buffers, depth buffers, and custom shaders.

### Shader pipeline

Shaders are written in GLSL and compiled at build time:

```
shaders/foo.vert  ──glslc/glslangValidator──►  foo.vert.spv  ──spirv-cross──►  foo.vert.msl
shaders/foo.frag  ──glslc/glslangValidator──►  foo.frag.spv  ──spirv-cross──►  foo.frag.msl
```

All compiled outputs land in `build/<preset>/shaders/` and are copied next to the binary at
build time. At startup the renderer queries `SDL_GetGPUShaderFormats` to determine which
backend is active and loads `.spv` (Vulkan) or `.msl` (Metal) accordingly.

---

## ECS

`src/ecs/Registry.hpp` exposes a single `Registry` type:

- **`-DUSE_ENTT=ON`** → `Registry = entt::registry` (full EnTT API)
- **Default** → minimal stub; replace with your own implementation

---

## Code style

### Formatting

```bash
# Reformat all sources in-place (after cmake configure)
cmake --build --preset debug --target format

# Check without modifying (mirrors CI)
cmake --build --preset debug --target format-check
```

Format checking runs automatically:
- **Pre-commit hook** — auto-formats staged `.cpp`/`.hpp` files
- **Pre-push hook** — blocks the push if any file fails format check
- Hooks activate on first `cmake configure` (sets `core.hooksPath .githooks`)

Key rules (`.clang-format`): 4-space indent · 120 column limit · Allman braces · `int*` pointer style.

### Naming (`.clang-tidy`)

| Kind | Style | Example |
|---|---|---|
| Class / Struct | `CamelCase` | `Game`, `Renderer` |
| Function / Method | `camelBack` | `drawFrame()` |
| Variable / Parameter | `camelBack` | `deltaTime` |
| Member field | `camelBack` | `window` |
| Constant | `k_camelBack` | `k_winW` |
| Namespace | `lower_case` | `renderer` |

---

## CI

GitHub Actions runs on every push and PR:

| Job | Platforms | Notes |
|---|---|---|
| `build` | Ubuntu · macOS · Windows | Debug build with sanitizers |
| `format` | Ubuntu | `clang-format-18 --dry-run --Werror` — blocks merge |
| `tidy` | Ubuntu | `clang-tidy` — non-blocking while codebase grows |
| `release-build` | Ubuntu · macOS · Windows | Optimised build |
| `publish` | Ubuntu | Creates / updates GitHub Release |

Release binaries are published to GitHub Releases on every push to `main` (rolling `latest` pre-release) and on version tags `v*.*.*` (versioned release).

---

## Project structure

```
group2/
├── .github/workflows/ci.yml   # CI / CD pipeline
├── .githooks/                 # pre-commit (auto-format) + pre-push (format gate)
├── cmake/
│   ├── CompilerWarnings.cmake
│   └── Sanitizers.cmake
├── sanitizers/
│   └── lsan.supp              # LSan suppressions for SDL3 false positives
├── scripts/
│   ├── setup-linux.sh         # Debian/Ubuntu
│   ├── setup-archlinux.sh     # Arch / Manjaro / EndeavourOS
│   ├── setup-macos.sh         # macOS (Homebrew)
│   └── setup-windows.ps1      # Windows (winget + Vulkan SDK)
├── shaders/                   # GLSL source — compiled to SPIR-V at build time
│   ├── triangle.vert
│   └── triangle.frag
├── src/
│   ├── ecs/
│   │   └── Registry.hpp       # Registry type (EnTT or stub)
│   ├── game/
│   │   ├── Game.hpp           # Top-level game object — owns all subsystems
│   │   └── Game.cpp
│   ├── renderer/
│   │   ├── Renderer.hpp       # SDL3 GPU pipeline wrapper
│   │   └── Renderer.cpp
│   └── main.cpp               # SDL3 app callbacks → Game
├── .clang-format
├── .clang-tidy
├── .gitignore
├── CMakeLists.txt
└── CMakePresets.json
```

---

## Dependency versions

To update a dependency, change `GIT_TAG` in `CMakeLists.txt` and delete `build/` to force a re-fetch.

| Library | Tag | Notes |
|---|---|---|
| SDL3 | `release-3.2.0` | Window, GPU pipeline, input |
| SDL3_net | `main` | Networking |
| GLM | `1.0.1` | Math (vectors, matrices, quaternions) |
| EnTT | `v3.14.0` | ECS — only fetched when `USE_ENTT=ON` |
