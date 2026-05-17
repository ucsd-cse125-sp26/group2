# Web client port — design spec

**Status:** Draft — pending user review
**Date:** 2026-04-22
**Author:** design conversation (Claude)
**Scope:** M1–M5 tracer-bullet port of the existing `src/client` to run in-browser
via Emscripten + WebGPU, reusing the same renderer, shaders, and game loop.

---

## 1. Goal

Ship a browser-playable build of the existing Quake-style FPS client as a
reproducible example project demonstrating:

- SDL3 GPU API running in a browser via WebGPU (through the
  `klukaszek/SDL@emscripten-webgpu` fork).
- A single renderer codebase that runs on desktop (Vulkan/Metal/D3D12) **and**
  web (WebGPU) without a parallel rewrite.
- Graceful feature degradation on web via a `RendererCaps` probe.
- A dual-transport server (TCP + WebSocket) so the same desktop server serves
  both client kinds.
- Playable from a static host (itch.io or GitHub Pages) against a known server
  URL.

## 2. Non-goals

Explicitly out of scope for M1–M5:

- **Visual parity** with desktop. Passes that hit WebGPU limitations get
  disabled per-session and logged; we chase parity only after the demo ships.
- **ImGui debug overlay on web.** Disabled entirely; desktop keeps it.
- **KTX2/Basis texture transcoding, Meshopt/Draco mesh compression, streaming
  asset fetch.** All stretch.
- **Pthreads / SharedArrayBuffer.** Single-threaded wasm only.
- **Touch / mobile controls, gamepad.** Keyboard + mouse only (gamepad may
  work by accident via SDL3's Emscripten backend; not promised).
- **Production TLS (`wss://`), rate limiting, anti-cheat.** `wss://` with a
  real TLS cert is **required** for any non-localhost deployment (CWE-319:
  sending gameplay traffic over unencrypted WebSocket exposes it to MITM and
  leaks on any shared network). We document the Caddy-in-front pattern but
  don't stand it up in M1–M5. Local development on `127.0.0.1` / `localhost`
  uses the unsecured scheme because loopback traffic never leaves the host;
  any deployment reachable off-host must switch to `wss://` before shipping.
- **The `renderer-new/` refactor.** Web port uses the current
  `src/client/renderer/Renderer.cpp`.
- **Stable Safari** and **Chromium < 113.** Supported browsers: Chrome/Edge
  ≥ 113, Firefox Nightly (flag), Safari Technology Preview. Unsupported
  browsers show a "WebGPU not available" splash rather than attempting
  fallback.

## 3. Decisions recorded

Captured during brainstorming; these are settled unless explicitly revisited.

| # | Decision | Chosen |
|---|---|---|
| 1 | Fidelity strategy | **Mixed** — demo-first, parity as stretch |
| 2 | Server transport strategy | **Dual listener** — server accepts both TCP and WebSocket natively |
| 3 | ImGui on web | **Disabled** entirely |
| 4 | SDL fork strategy | Pin to `klukaszek/SDL` commit SHA `187eb153976736f7aa5a1d8ea6c39968e35032ca`; do **not** rebase onto current SDL main; fork later only if klukaszek's repo churns or we need to patch |
| 5 | Shader pipeline | Keep SPIR-V at build time; runtime SPIR-V→WGSL translation via `SDL_gpu_shadercross` + Tint on web |
| 6 | WebSocket library on server | **uWebSockets** (mature, secure defaults, no raw RFC 6455 implementation) |
| 7 | Asset delivery | Curated ~70 MB preload bundle via `--preload-file`; brotli/gzip on the wire |
| 8 | Implementation isolation | All implementation work happens in a **new git worktree** |

## 4. Architecture

```
                                        ┌─────────────────────────────┐
                                        │   src/client (shared C++)   │
                                        │   Game, ECS systems,        │
                                        │   Renderer.cpp (shared),    │
                                        │   shaders (*.spv shared)    │
                                        └──────┬──────────────────────┘
                                               │ depends on
                        ┌──────────────────────┼───────────────────────┐
                        ▼                      ▼                       ▼
              ┌──────────────────┐   ┌──────────────────┐   ┌──────────────────┐
              │   ITransport     │   │  IRenderer       │   │   IDebugUI       │
              │  (new interface) │   │ (already exists) │   │ (new interface)  │
              └─────┬────────┬───┘   └──────────────────┘   └─────┬────────┬───┘
                    │        │                                    │        │
          desktop ──┘        └── web              desktop ────────┘        └── web
          (SDL3_net TCP)  (emscripten WS)         (ImGui via sdlgpu3)     (no-op stub)

                         Renderer is the SAME on both — it just calls SDL_CreateGPUDevice.
                         On desktop that picks Vulkan/Metal/D3D12.
                         On web (via klukaszek/SDL fork) it picks WebGPU.
                         SPIR-V → WGSL conversion happens at runtime via libtint.

                                  ┌────────────────────────────────────┐
                                  │             src/server             │
                                  │   TCP listener (SDL3_net, native)  │
                                  │   WebSocket listener (uWebSockets) │
                                  │   Both feed same game loop         │
                                  └────────────────────────────────────┘
```

**Key principle:** one renderer, one shader set, one game loop. Platform
specifics live behind three interfaces: `ITransport`, `IRenderer` (exists),
`IDebugUI` (new). Web-specific regressions get expressed in data (a
`RendererCaps` struct), not in parallel code paths.

## 5. Milestones (tracer-bullet)

Each milestone is independently shippable. We can stop at any point and still
have something demonstrable. Effort estimates are rough and will drift — the
value is the ordering, not the hours.

| # | Goal | Effort | Proves |
|---|---|---|---|
| **M1** | Blank SDL window rendered in the browser via the fork | ~hours | Toolchain end-to-end: emsdk, fork, CMake web preset, asset preload, shell HTML |
| **M2** | One pipeline: draw `geometry.vert/frag` with a single model | ~1 day | SPIR-V → WGSL at runtime via shadercross/Tint; basic buffer + texture binding |
| **M3** | Full renderer init with `RendererCaps` gating | few days | The 50-odd pipelines either build or are cleanly disabled; we learn what actually works on WebGPU and what doesn't |
| **M4** | Networking: `ITransport` split, server WS listener, client connects + plays | ~1 day | Full game loop including input, prediction, reconciliation over WebSocket |
| **M5** | Polish, asset curation, hosting, README | few days | A link that someone can click and play; write-up of what we learned |

Milestones **do not** include ImGui-on-web, mobile, or TLS — those are
post-M5 stretch work.

## 6. Build system

### 6.1. New CMake preset `web`

Added to `CMakePresets.json`:

- `CMAKE_TOOLCHAIN_FILE=$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`
- Binary dir: `build/web/`
- Generator: Ninja
- Cache variable `GROUP2_WEB_CLIENT=ON`

Also `web-release` for the `-O3` bundle.

### 6.2. CMakeLists.txt gates

All web branches gated by `if(EMSCRIPTEN)` — zero changes to the desktop code
path.

1. **SDL source swap** inside `if(EMSCRIPTEN)`:

   ```cmake
   FetchContent_Declare(SDL3
       GIT_REPOSITORY https://github.com/klukaszek/SDL.git
       GIT_TAG        187eb153976736f7aa5a1d8ea6c39968e35032ca
       GIT_SHALLOW    FALSE)
   ```

2. **`SDL_gpu_shadercross` + Tint** fetched and built as a static lib, linked
   only on web.

3. **`SDL3_net`, `imgui_lib`, assimp-desktop-tools** — skipped on web.

4. **Emscripten link flags** applied to the `client` target:

   ```
   -sUSE_WEBGPU=1
   -sASYNCIFY=1
   -sALLOW_MEMORY_GROWTH=1
   -sINITIAL_MEMORY=256MB
   -sSTACK_SIZE=1MB
   -sMODULARIZE=1
   -sEXPORT_ES6=1
   -sEXPORT_NAME=createGame
   -sENVIRONMENT=web
   -sDISABLE_EXCEPTION_CATCHING=0
   -fwasm-exceptions
   --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html
   --preload-file ${WEB_ASSETS_DIR}@/assets
   --preload-file ${CMAKE_BINARY_DIR}/shaders@/shaders
   ```

5. **Output**: `client.html`, `client.js`, `client.wasm`, `client.data` under
   `build/web/`.

6. **Sanity guards**: fatal-error if CMake < 3.25 or emcc < 3.1.69.

### 6.3. Developer workflow

```bash
source $EMSDK/emsdk_env.sh
cmake --preset web
cmake --build --preset web
# run the game server on the desktop binary
./build/debug/server --config config.toml &
# serve the static bundle
emrun --no_browser --port 8000 build/web/client.html
# open http://localhost:8000/client.html?server=<loopback-websocket-url>
# (the default `?server=` value — see §12.2 — is auto-derived from the page origin,
#  so you usually don't need to pass it for localhost dev)
```

Captured in `scripts/dev-web.sh` as a one-shot.

## 7. SDL fork integration

- **Fork pinning:** we do not yet fork klukaszek's repo; we pin to his
  commit SHA. We will fork only if the upstream branch churns or we need to
  patch. Rebasing onto current SDL main is explicitly not in scope — the
  branch diverged April 2025 and the conflict surface is concentrated
  exactly in the modified GPU driver files; this would be a multi-week
  research project with low demo payoff, and the SDL team indicated the
  architecture itself needs rework.
- **If we do need to patch**, the workflow is: fork klukaszek's repo on
  GitHub, push a named branch (e.g. `webgpu-pinned-for-group2`) at his SHA,
  apply the minimal patch, update `GIT_REPOSITORY` + `GIT_TAG` in our
  CMakeLists. Cherry-pick from SDL mainline only in surgical, clearly-scoped
  commits — never a bulk rebase.

## 8. Shader pipeline on web

### 8.1. Code changes (minimal)

Two spots change in `src/client/renderer`:

**`Renderer.cpp` ~L1595–1640 (device init):** add an `HAVE_WGSL_SHADERS`
branch gated by Emscripten that includes `SDL_GPU_SHADERFORMAT_WGSL` in
`k_wantedFormats`, and extends the format-preference cascade.

**`ShaderUtils.cpp` + the Renderer-local shader loader (~L150–220, currently
duplicated):** consolidate into one helper that, on web, routes SPIR-V through
`SDL_ShaderCross_*` to produce WGSL before calling `SDL_CreateGPUShader`. The
exact shadercross function signature is verified at M2 when we first link it.

Entry point: WGSL uses `"main"`; the `"main0"` special case is MSL-only.

### 8.2. Shader-level risks (validated at M2)

Probed by a one-page sanity sweep of `shaders/`:

| Risk | Where it fails | Mitigation |
|---|---|---|
| Push constants → synthetic UBO binding (e.g. `(3,0)`) | Clashes with existing bindings | Grep pipelines; rename if needed |
| `std430` in uniform buffers | WebGPU UBOs must be `std140` | Audit GLSL; switch to `std140` or move to storage buffer |
| `rgba16f` storage textures | Needs adapter feature | `RendererCaps` probe; disable affected passes |
| UBO size > 64 KB | Large bone/matrix arrays | Count floats in biggest UBO; split if needed |
| Workgroup total > 256 | Compute shaders | Grep `.comp` `local_size`; reshape tile |
| Dynamic sampler-array indexing | Requires feature | Grep; rewrite if present |
| `atomicAdd(float)` | Not always supported | Replace with `CAS` loop |

Findings + mitigations captured in an M2 sweep report committed to
`docs/superpowers/` for the M3 capability gating.

## 9. Renderer capabilities — `RendererCaps`

New file `src/client/renderer/RendererCaps.hpp` holds:

- Device capability booleans (storage format support, atomic float, large
  workgroup, indirect draw, depth-as-storage, MSAA resolve).
- Per-pass enables for every risky pass (bloom, SSAO, GTAO, SSR, SSS, TAA,
  SMAA, volumetrics, OIT, CAS, motion vectors).
- Two factory presets: `desktopDefault()` (all true) and `webDefault()`
  (conservative — TAA off by default as it's the most likely to misbehave;
  everything else on).

**Flow:**

1. Choose preset by platform at device init.
2. Probe device format + feature support; AND out caps that fail.
3. Apply user-config / URL-query overrides (developer escape hatch such as
   `?caps=no-bloom,no-taa`).
4. For each enabled pass, try-init in a fallible wrapper; on failure, log
   loudly and disable for the session.
5. Render-time conditionals (`if (caps.enableBloom) bloom.render(...)`) gate
   each pass. Once disabled, a pass stays disabled until next launch — no
   per-frame dynamic re-enable.

**Startup report:** print a summary table to console (and to the HTML shell's
diagnostics panel) listing enabled vs. disabled passes with the reason. This
is the visible degradation report — cheap to produce, essential for triage.

**Reuse on desktop:** the same machinery is useful for low-end iGPUs and for
iterating on new passes. Not a web-only tax.

## 10. Transport abstraction

### 10.1. New interface `src/network/ITransport.hpp`

Owns `connect`/`close`/`isOpen`/`sendFrame`/`poll(FrameHandler)`/stats
accessors. Frame semantics match `MessageStream`'s length-prefixed framing on
TCP so the wire bytes are byte-for-byte identical between transports; on
WebSocket, each logical message is one binary frame.

### 10.2. Desktop implementation `TcpTransport`

Wraps the existing `NET_StreamSocket` + `MessageStream` code extracted from
`Client.cpp`. ~30 minutes of refactoring.

### 10.3. Web implementation `WebSocketTransport`

Uses `emscripten_websocket_*` — event-driven, does **not** require Asyncify.
Inbox is a simple deque populated by the `onMessage` callback; `poll` drains
it into the `FrameHandler`. Binary frames only; URL validation at construct
time (scheme must be `wss://` for any non-loopback host; the unsecured
WebSocket scheme is accepted only when the host is `localhost` or
`127.0.0.1`).

### 10.4. Client-side glue

`src/client/network/Client.hpp` becomes transport-agnostic — owns
`std::unique_ptr<ITransport>` instead of the SDL3_net socket. `PacketType`,
`RegistrySerialization`, `ShotEvent`, `NetworkStats`, RTT measurement — all
unchanged.

### 10.5. Server-side dual listener

Existing TCP listener (SDL3_net) stays. A new WebSocket listener built on
**uWebSockets** runs alongside it in the same main loop (uWebSockets supports
external-loop integration — no new threads).

**Security defaults** (explicit, aligned with the "secure libraries by
default" guidance):

- `maxPayloadLength`: capped (64 KB, sized to the largest registry snapshot
  with headroom).
- `maxBackpressure`: capped so slow clients can't balloon memory.
- `idleTimeout`: 30 s.
- `Origin` header whitelist; reject others with HTTP 403 at handshake.
- Binary-only frames; text frames dropped.
- `perMessageDeflate`: off (CPU cost + historical CVE surface).

Each accepted WS connection is wrapped in a `WebSocketServerTransport`
implementing `ITransport` and handed to the same `onSessionConnected`
function the TCP path uses.

### 10.6. Config

`config.toml` `[server-network]` gains `tcp_port`, `ws_port`, and
`ws_allowed_origins`. Back-compat: missing `tcp_port` falls back to the
existing `port` key.

## 11. Asset strategy

### 11.1. Curated web bundle

`web/web-assets.manifest` lists the minimal asset subset for the web build.
Target ~70 MB uncompressed, ~45–55 MB on the wire with brotli.

Initial manifest:

- 1 map: `metallic_pallet_factory_store.glb` (~10 MB)
- 1 character: `Apex_Legend_Wraith.glb` (~21 MB)
- 1 weapon: `r-301_-_apex_legends.glb` (~29 MB)
- Selected animations the character actually uses
- 1 HDRI, **downsampled 4K → 2K**, committed under
  `assets/web-downsampled/` (~6 MB)
- Shaders + small config (~3 MB)

Duplicate/unused heavy assets (71 MB Porsche, duplicate Wraith) stay
desktop-only.

### 11.2. Build pipeline

CMake reads the manifest, copies listed files into `build/web/web-assets/`
preserving tree structure. HDRI downsample is pre-committed to `assets/` (not
generated at build time) for simplicity. The `--preload-file` flag maps
`build/web/web-assets/` onto `/assets/` inside MEMFS, so existing
`std::filesystem` paths work unchanged.

### 11.3. Loading UX

Preload-file with a custom `web/shell.html` showing a progress bar driven by
`Module.setStatus`. ~10–30 sec on broadband first load; browser-cached
thereafter.

### 11.4. Writable paths

| Writer | Desktop | Web |
|---|---|---|
| `FrameRecorder` | File-backed | `#if !__EMSCRIPTEN__` — disabled |
| `imgui.ini` | File-backed | n/a (ImGui disabled) |
| `config.toml` | Read at startup | Read-only preload |
| Future user settings (volume, bindings) | out of scope for M1–M5 | **IDBFS** at `/persist` — path reserved and mount is set up, but nothing writes to it in M1–M5 |

### 11.5. Compression

emcc `--preload-file-compression` (brotli) on the `.data` file. Web server
must serve with `Content-Encoding: br` — works on custom hosts; GitHub Pages
requires falling back to gzip.

### 11.6. Out of scope

Streaming assets; KTX2/Basis transcoding; meshopt/Draco mesh compression.

## 12. HTML shell, hosting, security headers

### 12.1. `web/shell.html`

Purpose-built, ~150 lines:

1. Canvas fills viewport.
2. Progress bar hooked to `Module.setStatus`.
3. **"Click to Play"** overlay — first click triggers
   `canvas.requestPointerLock()` (browsers require a user gesture for pointer
   lock; there's no way around it).
4. **Diagnostics panel** (`<details>`, collapsed) showing the `RendererCaps`
   degradation report and a console link.

### 12.2. Server URL discovery

Query-string driven, sensible default:

- Parse `?server=<websocket-url>` from `location.search` in shell JS;
  validate scheme (accept `wss://` for any host; accept the unsecured
  WebSocket scheme **only** when host is `localhost` or `127.0.0.1`); pass
  as `arguments:[...]` on `Module`.
- Default if omitted: `wss://<current host>:9998` when page is HTTPS; the
  unsecured equivalent only when page is loopback HTTP (dev loop).

### 12.3. Security headers (production hosting)

| Header | Value |
|---|---|
| `Content-Security-Policy` | `default-src 'self'; connect-src 'self' ws: wss:; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; object-src 'none'; base-uri 'self'; frame-ancestors 'none'` |
| `Cross-Origin-Opener-Policy` | `same-origin` |
| `Cross-Origin-Embedder-Policy` | `require-corp` |
| `X-Content-Type-Options` | `nosniff` |
| `Referrer-Policy` | `no-referrer` |
| `Permissions-Policy` | `fullscreen=(self), gamepad=(self)` |

For production, `connect-src ws:` narrows to the explicit `wss://your.server`
origin. MIME: `.wasm → application/wasm`, `.data` with `Content-Encoding` as
compressed.

### 12.4. Hosting

- **Default pick for the demo:** itch.io — game-friendly, large bundles OK.
- **Mirror / source story:** GitHub Pages (bundle needs to fit 100 MB
  per-file limits; gzip since GH Pages doesn't serve brotli for `.data`).
- **Custom (Caddy/nginx):** full header + TLS control if the demo gets
  serious.

WebSocket scheme matches page scheme: HTTPS page ⇒ `wss://` server, which
means TLS termination (Caddy in front is the easiest path). Local
development uses the unsecured WebSocket scheme over loopback HTTP only —
never on an address reachable off the host.

### 12.5. Pre-launch checklist (M5)

- `ws_allowed_origins` matches hosted page URL.
- TLS terminator up if page is HTTPS.
- CSP `connect-src` narrowed from `ws:` to the explicit `wss://` origin.
- `.data` precompressed; correct `Content-Encoding` on the host.
- Source-repo link in page `<head>` — this is an example project.

## 13. Implementation isolation

All implementation happens in a **new git worktree** (per user request),
using the `superpowers:using-git-worktrees` workflow. Desktop development on
`main` continues in parallel and uninterrupted.

## 14. Open items — resolved at specific milestones

Items we deliberately don't settle in the spec because the cheapest way to
answer is to try it:

- **Exact `SDL_ShaderCross_*` function signature** — verified at M2.
- **Precise list of passes that must be disabled on WebGPU** — determined by
  the M2 shader sanity sweep + M3 try-init results. Logged in the
  `RendererCaps` startup report.
- **Exact binary size of libtint in the wasm** — measured at M2; expected
  ~2–3 MB.
- **Whether gamepad works out of the box** via SDL3's Emscripten backend —
  check at M4.

## 15. Success criteria

- **M5 done:** a public link opens a page; a user clicks, pointer lock
  engages, a character loads, they connect to a known server via WebSocket,
  they see other players, they can move and shoot.
- **Desktop unchanged:** zero regressions on Linux / macOS / Windows. Same
  CMake presets, same shaders, same renderer behavior, same packet format.
- **Degradation visible:** if some passes are disabled on web, the shell
  diagnostics panel names them.
- **Reproducible:** `cmake --preset web && cmake --build --preset web` works
  from a fresh clone + `emsdk` install.
- **Documented:** a short post-M5 writeup in `docs/` explaining what worked,
  what didn't, which WebGPU limitations we hit.
