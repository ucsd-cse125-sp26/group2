# group2

CSE 125 Spring 2026 — C++23, SDL3, ECS.

```bash
# Linux
bash scripts/setup-linux.sh
cmake --preset debug && cmake --build --preset debug
LSAN_OPTIONS=suppressions=sanitizers/lsan.supp ./build/debug/group2

# macOS
bash scripts/setup-macos.sh
cmake --preset debug && cmake --build --preset debug
./build/debug/group2

# Windows (run setup-windows.ps1 first)
cmake --preset debug-win && cmake --build --preset debug-win
.\build\debug-win\group2.exe
```

## Tech stack

| Concern | Library |
|---|---|
| Window / Input | [SDL3](https://github.com/libsdl-org/SDL) |
| Rendering | SDL3 Renderer API (GPU-accelerated 2D, software-projected 3D) |
| Networking | [SDL3_net](https://github.com/libsdl-org/SDL_net) |
| ECS (optional) | [EnTT](https://github.com/skypjack/entt) (`-DUSE_ENTT=ON`) or roll your own |
| Math | [GLM](https://github.com/g-truc/glm) |
| Build | CMake 3.25+ · Ninja · MSVC 2022 (Windows) · Clang (Linux/macOS) |
| Sanitizers | ASan + UBSan (debug Linux/macOS), ASan (Windows MSVC) |
| Lint | clang-format-18 · clang-tidy |
| CI | GitHub Actions (Ubuntu · macOS · Windows) |

All dependencies are fetched automatically via CMake `FetchContent` — **no system installs needed for the libraries themselves**.

---

## Prerequisites

### Linux — Debian / Ubuntu

```bash
bash scripts/setup-linux.sh
```

### Linux — Arch Linux (and derivatives: Manjaro, EndeavourOS, CachyOS…)

```bash
bash scripts/setup-archlinux.sh
```

Both scripts install: `cmake`, `ninja`, `clang`, `clang-format-18`, `clang-tidy-18`, and all SDL3 system-level headers (X11, Wayland, ALSA, Pulse, etc.).

### macOS

```bash
bash scripts/setup-macos.sh
```

Requires Xcode Command Line Tools (provides clang/Metal) + [Homebrew](https://brew.sh) (`cmake`, `ninja`, `llvm@18`).

### Windows

Run from an **elevated PowerShell**:

```powershell
Set-ExecutionPolicy Bypass -Scope Process -Force
.\scripts\setup-windows.ps1
```

The script installs everything needed via `winget`: **VS Build Tools 2022** (MSVC compiler + Windows SDK — auto-installed if no VS 2022 is found), CMake, Ninja, LLVM (clang-format/clang-tidy), and the Vulkan SDK.

> **Note — git tag fetch:** The setup scripts also run `git config --add remote.origin.fetch "+refs/tags/*:refs/tags/*"`, which prevents `git pull` from failing with "would clobber existing tag". If you cloned before running a setup script, run that one command manually.

---

## Building

All commands run from the repo root.

### Linux / macOS

```bash
# Debug — with AddressSanitizer + UBSan
cmake --preset debug
cmake --build --preset debug

# Release
cmake --preset release
cmake --build --preset release
```

The binary lands in `build/<preset>/group2`.

### Windows

```powershell
cmake --preset debug-win
cmake --build --preset debug-win
```

The binary lands in `build\debug-win\group2.exe`. No Developer PowerShell needed — the CMake toolchain file auto-detects MSVC via `vswhere`.

---

## IDE setup

### CLion
Open the **repo root folder** in CLion. It reads `CMakePresets.json` automatically.
The preset profiles are pre-enabled via `.idea/` files committed in this repo
and should appear already checked in **Settings > Build, Execution, Deployment > CMake**.
CLion evaluates the preset conditions and shows only the presets matching your platform
(`debug-win`, `release-win`, `relwithdebinfo-win` on Windows; `debug`, `release`,
`relwithdebinfo` on Linux/macOS).

If CLion has also added its own "Debug" profile (pointing at `cmake-build-debug/`), delete
it from that settings page and keep only the preset-based ones.

> **Note:** CLion modifies `workspace.xml` locally as you work (run configs, UI state).
> Do **not** commit those changes — only commit deliberate edits to the `CMakeSettings` block.

### VS Code
1. Install the recommended extensions when prompted (`.vscode/extensions.json` is committed).
   The key extension is **CMake Tools** (`ms-vscode.cmake-tools`).
2. CMake Tools detects `CMakePresets.json` automatically (`cmake.useCMakePresets: "always"` is set).
3. Select a preset from the status bar — `debug` on Linux/macOS, `debug-win` on Windows.
4. **Build:** `Ctrl+Shift+B` or the build button in the CMake status bar.
5. **Debug:** `F5` → pick **Launch group2 (Linux / macOS)** or **Launch group2 (Windows)**.

### Visual Studio 2022
Use **File › Open › Folder** (not *Open › Project/Solution*) to open the repo root.
VS 2022 reads `CMakePresets.json` natively — `debug-win`, `release-win`, and
`relwithdebinfo-win` appear in the configuration dropdown at the top of the window.

1. Select **debug-win** from the configuration dropdown.
2. **Build:** `Ctrl+Shift+B`.
3. **Run / Debug:** `F5` — VS 2022 auto-detects the `group2.exe` CMake target.
   (`launch.vs.json` in the repo root provides the explicit debug entry.)

---

## Running

```bash
./build/debug/group2      # Linux / macOS
.\build\debug-win\group2  # Windows
```

Press **Escape** or close the window to quit.

**LSan false positives on Linux:** SDL3's Linux backends (dbus, Wayland) perform intentional one-time allocations that ASan reports as leaks. Suppress them with:

```bash
LSAN_OPTIONS=suppressions=sanitizers/lsan.supp ./build/debug/group2
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
# EnTT ECS
cmake --preset debug -DUSE_ENTT=ON
```

---

## Rendering

The renderer (`src/renderer/Renderer`) wraps SDL3's Renderer API — a GPU-accelerated
2D drawing layer that works on Vulkan, Metal, and Direct3D without requiring custom shaders
or a shader compilation toolchain.

3D geometry is projected on the CPU using GLM (MVP transform → NDC → screen space) and
drawn via `SDL_RenderGeometry`. Triangles are depth-sorted per frame (painter's algorithm).

---

## ECS

`src/ecs/Registry.hpp` exposes a single `Registry` type:

- **`-DUSE_ENTT=ON`** → `Registry = entt::registry` (full EnTT API)
- **Default** → minimal stub class; replace with your own implementation

---

## Code style

### Formatting

```bash
# Reformat all sources in-place (after cmake configure)
cmake --build --preset debug --target format

# Check without modifying (mirrors CI)
cmake --build --preset debug --target format-check
```

Format checking is also enforced automatically:
- **Pre-commit hook** — auto-formats staged `.cpp`/`.hpp` files
- **Pre-push hook** — blocks the push if any file fails format check
- Hooks activate automatically when you run `cmake configure` (sets `core.hooksPath .githooks`)

Key rules (see `.clang-format`): 4-space indent · 120 column limit · Allman braces · `int*` pointer style.

### Naming (`.clang-tidy`)

| Kind | Style | Example |
|---|---|---|
| Class / Struct | `CamelCase` | `Game` |
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
│   ├── setup-linux.sh
│   ├── setup-archlinux.sh
│   ├── setup-macos.sh
│   └── setup-windows.ps1
├── src/
│   ├── ecs/
│   │   └── Registry.hpp       # Registry type (EnTT or stub)
│   ├── game/
│   │   ├── Game.hpp           # Top-level game object (init/event/iterate/quit)
│   │   └── Game.cpp
│   ├── renderer/
│   │   ├── Renderer.hpp       # SDL3 Renderer wrapper + 3D projection
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

To update a dependency, change the `GIT_TAG` in `CMakeLists.txt` and delete `build/` to force a re-fetch.

| Library | Tag | Condition |
|---|---|---|
| SDL3 | `release-3.2.0` | always |
| SDL3_net | `main` | always |
| GLM | `1.0.1` | always |
| EnTT | `v3.14.0` | `USE_ENTT=ON` |
