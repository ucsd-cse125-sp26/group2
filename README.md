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

# Windows (Developer PowerShell for VS 2022)
cmake --preset debug-win && cmake --build --preset debug-win
.\build\debug-win\group2.exe
```

## Tech stack

| Concern | Library |
|---|---|
| Window / Input / GPU | [SDL3](https://github.com/libsdl-org/SDL) — Vulkan · Metal · DX12 via SDL GPU API |
| Graphics (optional) | OpenGL 4.1 core profile via [glad](https://github.com/Dav1dde/glad) (`-DUSE_OPENGL=ON`) |
| ECS | [EnTT](https://github.com/skypjack/entt) (always enabled) |
| Math | [GLM](https://github.com/g-truc/glm) |
| Build | CMake 3.25+ · Ninja |
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

Visual Studio 2022 is a manual prerequisite (free Community edition). The script installs `cmake`, `ninja`, and LLVM via `winget`.

---

## Building

All commands run from the repo root.

### Linux / macOS

```bash
# Debug — with AddressSanitizer + UBSan
cmake --preset debug
cmake --build --preset debug

# Release (shaders embedded in binary)
cmake --preset release
cmake --build --preset release
```

The binary lands in `build/<preset>/group2`.

### Windows

Open **Developer PowerShell for VS 2022**, then:

```powershell
cmake --preset debug-win
cmake --build --preset debug-win
```

The binary lands in `build\debug-win\group2.exe`.

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
| `USE_OPENGL` | `OFF` | Use OpenGL 4.1 core backend (glad) instead of SDL3 GPU pipeline |
| `GROUP2_BUNDLE_SHADERS` | `ON` in Release | Embed SPIR-V shaders into the binary (SDL3 GPU path only) |
| `ENABLE_ASAN` | `OFF` | AddressSanitizer (on by default in `debug` preset) |
| `ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer (on by default in `debug` preset) |
| `ENABLE_TSAN` | `OFF` | ThreadSanitizer (mutually exclusive with ASan) |

```bash
# OpenGL backend
cmake --preset debug -DUSE_OPENGL=ON
```

---

## Rendering backends

Two backends share the same `IRenderer` interface (`src/renderer/IRenderer.hpp`):

| Backend | Flag | Shader format | Notes |
|---|---|---|---|
| **SDL3 GPU** *(default)* | *(none)* | SPIR-V (compiled from GLSL at build time) | Runs on Vulkan · Metal · DX12 |
| **OpenGL 4.1 core** | `-DUSE_OPENGL=ON` | Inline GLSL (compiled at runtime) | macOS compatible; no SPIR-V toolchain needed |

The SDL3 GPU backend requires a GLSL→SPIR-V compiler at build time (`glslc` or `glslangValidator`). The OpenGL backend has no such requirement — shaders are embedded as C++ string literals.

---

## ECS

`src/ecs/Registry.hpp` exposes a single `Registry` type — `Registry = entt::registry` (EnTT is always enabled).

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
| Class / Struct | `CamelCase` | `AppState` |
| Function / Method | `camelBack` | `renderFrame()` |
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
| `release-build` | Ubuntu · macOS · Windows | Optimised build, shaders embedded |
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
│   ├── EmbedShaders.cmake     # embeds SPIR-V into the binary for Release
│   └── Sanitizers.cmake
├── sanitizers/
│   └── lsan.supp              # LSan suppressions for SDL3 false positives
├── scripts/
│   ├── setup-linux.sh
│   ├── setup-archlinux.sh
│   ├── setup-macos.sh
│   └── setup-windows.ps1
├── shaders/                   # GLSL source (compiled to SPIR-V at build time)
│   ├── triangle.vert
│   └── triangle.frag
├── src/
│   ├── ecs/
│   │   └── Registry.hpp       # Registry type (EnTT or stub)
│   ├── renderer/
│   │   ├── IRenderer.hpp      # Pure-virtual backend interface
│   │   ├── SDLGPURenderer.hpp/cpp  # SDL3 GPU pipeline backend
│   │   └── OpenGLRenderer.hpp/cpp  # OpenGL 4.1 core backend
│   └── main.cpp               # Entry point (SDL3 app callbacks)
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
| GLM | `1.0.1` | always |
| EnTT | `v3.14.0` | always |
| glad | `v0.1.36` | `USE_OPENGL=ON` |
