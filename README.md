# titandoom

3D multiplayer real-time FPS — C++23, SDL3, ECS.

```bash
# Linux
bash scripts/setup-linux.sh
cmake --preset debug && cmake --build --preset debug
LSAN_OPTIONS=suppressions=sanitizers/lsan.supp ./build/debug/titandoom

# macOS
bash scripts/setup-macos.sh
cmake --preset debug && cmake --build --preset debug

# Windows (Developer PowerShell for VS 2022)
cmake --preset debug-win && cmake --build --preset debug-win
```

## Tech stack

| Concern | Library |
|---|---|
| Window / Input / GPU | [SDL3](https://github.com/libsdl-org/SDL) (Vulkan · Metal · DX12 via SDL GPU API) |
| ECS | [EnTT](https://github.com/skypjack/entt) |
| Math | [GLM](https://github.com/g-truc/glm) |
| Shaders (optional) | [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross) |
| Build | CMake 3.25+ · Ninja |
| Sanitizers | ASan + UBSan (debug builds on Linux/macOS), ASan (Windows MSVC) |
| Lint | clang-format · clang-tidy |
| CI | GitHub Actions (Ubuntu · macOS · Windows) |

All dependencies are fetched automatically via CMake `FetchContent` — **no system installs are needed for the libraries themselves**.

---

## Prerequisites

### Linux (Debian / Ubuntu)

```bash
bash scripts/setup-linux.sh
```

This installs: `cmake`, `ninja`, `clang`, `clang-format`, `clang-tidy`, and all SDL3 system-level headers (X11, Wayland, ALSA, Pulse, etc.).

### macOS

```bash
bash scripts/setup-macos.sh
```

Requires Xcode Command Line Tools (provides clang/Metal) + Homebrew (`cmake`, `ninja`, `clang-format`).

### Windows

Run from an **elevated PowerShell**:

```powershell
Set-ExecutionPolicy Bypass -Scope Process -Force
.\scripts\setup-windows.ps1
```

This installs Visual Studio 2022 is a manual prerequisite (free Community edition). The script then installs `cmake`, `ninja`, and LLVM via `winget`.

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

The binary lands in `build/<preset>/titandoom`.

### Windows

Open **Developer PowerShell for VS 2022**, then:

```powershell
cmake --preset debug-win
cmake --build --preset debug-win
```

The binary lands in `build\debug-win\titandoom.exe`.

---

## Running

```bash
./build/debug/titandoom      # Linux / macOS
.\build\debug-win\titandoom  # Windows
```

Press **Escape** or close the window to quit.

**LSan false positives on Linux:** SDL3's Linux backends (dbus, Wayland) perform intentional one-time allocations that ASan reports as leaks. Suppress them with:

```bash
LSAN_OPTIONS=suppressions=sanitizers/lsan.supp ./build/debug/titandoom
```

---

## Code style

### Formatting

Format all source files in-place:

```bash
find src -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) \
  | xargs clang-format -i
```

Check without modifying (mirrors CI):

```bash
find src -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) \
  | xargs clang-format --dry-run --Werror
```

Key rules (see `.clang-format`): 4-space indent · 120 column limit · Allman braces · `int*` pointer style.

### Tidy

```bash
# After a configure step (compile_commands.json must exist)
find src -name "*.cpp" | xargs clang-tidy -p build/debug
```

Naming conventions (see `.clang-tidy`):

| Kind | Style |
|---|---|
| Class / Struct | `CamelCase` |
| Function / Method | `camelCase` |
| Variable / Parameter | `camelCase` |
| Member field | `m_camelCase` |
| Constant / Enum value | `UPPER_CASE` |
| Namespace | `lower_case` |

---

## CMake options

| Option | Default | Description |
|---|---|---|
| `ENABLE_ASAN` | `OFF` | AddressSanitizer (on by default in `debug` preset) |
| `ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer (on by default in `debug` preset) |
| `ENABLE_TSAN` | `OFF` | ThreadSanitizer (mutually exclusive with ASan) |
| `ENABLE_SHADERCROSS` | `OFF` | Build with SDL_shadercross for HLSL → SPIR-V/MSL/DXIL |

Override any option at configure time:

```bash
cmake --preset release -DENABLE_SHADERCROSS=ON
```

---

## Shaders

Shader source files live in `shaders/`. SDL3's GPU API accepts:

- **SPIR-V** (Vulkan, Linux/Windows)
- **MSL** (Metal, macOS/iOS)
- **DXIL** (DX12, Windows)

Enable SDL_shadercross to compile HLSL to all three targets at build time or runtime. See the [SDL_shadercross docs](https://github.com/libsdl-org/SDL_shadercross) for setup details once shaders are needed.

---

## CI

GitHub Actions runs on every push and PR:

| Job | Platforms | Notes |
|---|---|---|
| `build` | Ubuntu · macOS · Windows | Compiles with sanitizers enabled |
| `format` | Ubuntu | `clang-format --dry-run --Werror` — blocks merge |
| `tidy` | Ubuntu | `clang-tidy` report — non-blocking while codebase stabilises |

---

## Project structure

```
titandoom/
├── .github/workflows/ci.yml   # CI pipeline
├── cmake/
│   ├── CompilerWarnings.cmake
│   └── Sanitizers.cmake
├── scripts/
│   ├── setup-linux.sh
│   ├── setup-macos.sh
│   └── setup-windows.ps1
├── shaders/                   # HLSL shader source
├── src/
│   └── main.cpp               # Entry point (SDL3 callbacks)
├── .clang-format
├── .clang-tidy
├── .gitignore
├── CMakeLists.txt
└── CMakePresets.json
```

---

## Dependency versions

To update a dependency, change the `GIT_TAG` in `CMakeLists.txt` and delete `build/` to force re-fetch.

| Library | Tag |
|---|---|
| SDL3 | `release-3.2.0` |
| EnTT | `v3.14.0` |
| GLM | `1.0.1` |
