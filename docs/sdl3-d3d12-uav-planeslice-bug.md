# SDL3 D3D12 UAV PlaneSlice Bug — Investigation & Fix

## TL;DR

SDL3 3.2.0's D3D12 backend leaves one field of a UAV descriptor uninitialized when creating multi-layer textures (2D arrays / cubemaps) with compute-storage usage. In debug builds MSVC's `/RTC1` zero-fills the stack and the bug is invisible. In release builds (`/O2`), the field holds stack garbage, the GPU driver treats the resulting `CreateUnorderedAccessView` call as a fatal error, and the D3D12 device silently transitions to `DEVICE_REMOVED`. Every subsequent `SDL_CreateGPU*` call fails with `0x887A0001 (DXGI_ERROR_INVALID_CALL)`.

The fix is a one-line addition to vendored SDL3.

## Symptoms

Running `build/release/client.exe` on the D3D12 backend produced a wall of errors:

```
Renderer: GPU driver = direct3d12
Renderer: selected shader format = DXIL
IBL: failed to create irradiance cubemap: Failed to create texture!! ... (0x887A0001)
Renderer: IBL init failed
Renderer: compute pipeline 'bloom_downsample.comp' creation failed: Could not create root signature!
Renderer: compute pipeline 'bloom_upsample.comp' creation failed: Could not create root signature!
... (18 more pipeline failures)
HDR skybox: failed to create cubemap: Failed to create texture!! ... (0x887A0001)
GpuParticleBuffer: SDL_CreateGPUBuffer failed: Could not create buffer!! ... (0x887A0001)
GpuParticleBuffer: SDL_CreateGPUTransferBuffer failed: Could not create buffer!! ... (0x887A0001)
ParticleRenderer: smokeNoise texture failed: Failed to create texture!! ... (0x887A0001)
ParticleRenderer: pipeline particle_billboard.vert/particle_billboard.frag failed
ParticleRenderer: vertex pipeline ribbon.vert/ribbon.frag failed: ...
... (continues for every subsequent SDL_CreateGPU* call)
```

Critically:

- `build/debug/client.exe` worked fine.
- The **same source code** built into `build/release/client.exe` failed.
- The **first** failure was the irradiance cubemap creation in `Renderer::initIBL()`.
- After the first failure, **every** subsequent GPU resource creation failed with `0x887A0001`.

## Investigation

### Eliminating the obvious

Things that turned out **not** to be the cause:

| Hypothesis | Why ruled out |
|---|---|
| Stale build artifacts | `cmake --build` reported "no work to do"; `.obj` newer than source. |
| Mismatched defines between debug/release | `Renderer.cpp` had identical defines (`-DGLM_FORCE_DEPTH_ZERO_TO_ONE -DHAVE_DXIL_SHADERS -DNOMINMAX -DWIN32_LEAN_AND_MEAN`). |
| `SDL_build_config.h` differences | Byte-identical between configs. |
| D3D12 SDK Debug Layer instability with `debug_mode=true` | Setting `debug_mode=false` produced the exact same failure. |
| Specific texture parameters | Confirmed valid: 32×32, RGBA16F, cube, 1 mip, SAMPLER usage — textbook valid for D3D12. |
| Shader binding count mismatches | Audited every shader; all `num_samplers`/`num_storage_*`/`num_uniform_buffers` matched the shader source. |
| SDL3 version too old | SDL3 3.2.0 is the current stable release. |

So: identical source, identical defines, identical SDL3 source — only optimization flags differ.

When the same code works in debug and breaks in release, **undefined behavior** is almost always the answer. Most often: uninitialized memory, exposed by `/O2` reordering or by the absence of `/RTC1` zero-fill.

### Pinpointing the failure

The first failure was the irradiance cubemap. The two preceding texture creations (BRDF LUT, irradiance work map) succeeded. To narrow down whether the bug was specific to those parameters or to the cube call site, I added diagnostics:

```cpp
// Right before the failing cube creation, try params identical to BRDF LUT
// (which had succeeded as the first call):
SDL_GPUTextureCreateInfo ci2{};
ci2.type = SDL_GPU_TEXTURETYPE_2D;
ci2.format = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
ci2.width = 512;  ci2.height = 512;
ci2.layer_count_or_depth = 1;  ci2.num_levels = 1;
ci2.sample_count = SDL_GPU_SAMPLECOUNT_1;
ci2.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER;
SDL_GPUTexture* diag = SDL_CreateGPUTexture(device, &ci2);
SDL_Log("dup-BRDF-LUT result: %p err='%s'", (void*)diag, SDL_GetError());
```

**Result:** the dup-BRDF-LUT failed, even though the original BRDF LUT (with byte-identical params) had succeeded a few lines earlier. So the failure was not parameter-driven — **the device was already in a bad state.**

### Finding the corruption point

To find which call corrupts the device, I bracketed the suspect creations with stress tests using simple textures:

```cpp
// Right after ImGui_ImplSDLGPU3_Init — before any pipeline or texture work
for (int i = 0; i < 10; i++) {
    /* create+release a 16×16 R8G8B8A8 SAMPLER texture */
}
```

All 10 succeeded. Subsequent pipelines, the shadow map, the BRDF LUT, and the irradiance work map also succeeded. Then I added another stress block **immediately after** the irradiance work map:

```cpp
// AFTER the irradiance work map creation:
for (int i = 0; i < 5; i++) {
    /* identical 16×16 R8G8B8A8 SAMPLER stress texture */
    SDL_Log("POST-WORKMAP STRESS %d: %p err='%s'", i, ...);
}
```

**Result:** every post-workmap stress create returned `nullptr` with `0x887A0001`. The exact same params that succeeded before the work map now failed.

Then I tried skipping the work map entirely. With it skipped, the same 5 stress creates succeeded — and a dup-BRDF-LUT also succeeded. The cubemap with **CUBE type, 6 layers, RGBA16F, SAMPLER** still failed though, which initially looked like a second bug but turned out to be the same one (the next IBL texture, `prefilterWorkMap`, is also a 2D_ARRAY 6-layer COMPUTE_STORAGE_WRITE texture and was poisoning the device on its turn).

The pattern was now clear: **creating a 2D_ARRAY texture with `COMPUTE_STORAGE_WRITE` usage corrupts the D3D12 device** in release builds. The function returns success and a valid pointer, but every subsequent call fails.

### Root cause

`COMPUTE_STORAGE_WRITE` triggers the UAV creation path in `D3D12_INTERNAL_CreateTexture`. The relevant code in `.deps/release/sdl3-src/src/gpu/d3d12/SDL_gpu_d3d12.c` (around line 3432):

```c
if (needsUAV) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;   // <-- not zero-init

    D3D12_INTERNAL_AssignStagingDescriptorHandle(
        renderer, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        &texture->subresources[subresourceIndex].uavHandle);

    uavDesc.Format = SDLToD3D12_TextureFormat[createinfo->format];

    if (createinfo->type == SDL_GPU_TEXTURETYPE_2D_ARRAY ||
        createinfo->type == SDL_GPU_TEXTURETYPE_CUBE ||
        createinfo->type == SDL_GPU_TEXTURETYPE_CUBE_ARRAY) {
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.MipSlice        = levelIndex;
        uavDesc.Texture2DArray.FirstArraySlice = layerIndex;
        uavDesc.Texture2DArray.ArraySize       = 1;
        // BUG: PlaneSlice is never set!
    } else if (createinfo->type == SDL_GPU_TEXTURETYPE_3D) {
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.MipSlice    = levelIndex;
        uavDesc.Texture3D.FirstWSlice = 0;
        uavDesc.Texture3D.WSize       = depth;
    } else {
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice   = levelIndex;
        uavDesc.Texture2D.PlaneSlice = 0;   // <-- set here in the 2D branch
    }

    ID3D12Device_CreateUnorderedAccessView(
        renderer->device, texture->resource, NULL,
        &uavDesc, ...);
}
```

The struct definition in `d3d12.h`:

```cpp
typedef struct D3D12_TEX2D_ARRAY_UAV {
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
    UINT PlaneSlice;
} D3D12_TEX2D_ARRAY_UAV;
```

The 2D non-array branch sets `PlaneSlice = 0`. The 2D-array / cube branch sets `MipSlice`, `FirstArraySlice`, `ArraySize` — but **leaves `PlaneSlice` uninitialized**. Stack garbage flows into the descriptor.

When `PlaneSlice` happens to be a large junk value, the GPU driver receives an invalid view. Behavior is implementation-defined but in practice the device is silently marked `DEVICE_REMOVED`. Once removed, every subsequent D3D12 call returns `0x887A0001` — exactly what the user saw.

In debug builds:
- MSVC `/RTC1` zero-fills stack frames on entry, so `PlaneSlice` is `0` and the view is valid.

In release builds (`/O2 /Ob2 /DNDEBUG`):
- No zero-fill. `PlaneSlice` holds whatever was on the stack. Boom.

### The fix

A one-line addition to the 2D-array / cube branch:

```c
if (createinfo->type == SDL_GPU_TEXTURETYPE_2D_ARRAY ||
    createinfo->type == SDL_GPU_TEXTURETYPE_CUBE ||
    createinfo->type == SDL_GPU_TEXTURETYPE_CUBE_ARRAY) {
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    uavDesc.Texture2DArray.MipSlice        = levelIndex;
    uavDesc.Texture2DArray.FirstArraySlice = layerIndex;
    uavDesc.Texture2DArray.ArraySize       = 1;
    uavDesc.Texture2DArray.PlaneSlice      = 0;   // ADDED
}
```

Applied to both `.deps/release/sdl3-src/src/gpu/d3d12/SDL_gpu_d3d12.c` and `.deps/debug/sdl3-src/src/gpu/d3d12/SDL_gpu_d3d12.c`. After the patch, `build/release/client.exe` runs clean: IBL initializes, SMAA pipelines compile, the HDR skybox loads, particles work.

## Persistence note

If `.deps/{release,debug}/` is ever wiped or regenerated (`cmake --fresh`, `rm -rf .deps`, etc.) the patch is lost and the bug will return on release builds. Long-term options:

1. Pin a newer SDL3 once upstream fixes it.
2. Carry the patch as a `.patch` file applied automatically during the SDL3 fetch step in CMake.
3. Vendor SDL3 directly into the repo (heavier).

Until one of those is in place, the patch lives in the project memory note (`sdl3_d3d12_uav_planeslice_patch.md`) so future sessions know to reapply it.

## Should we upstream this?

**Yes — please do.** This is a small, isolated, clearly-correct fix to a real bug in upstream SDL3. Specifically:

- It's a **textbook uninitialized-field bug** with a one-line diff. SDL maintainers tend to merge those quickly.
- The fix has **no behavioral risk** for any caller — `PlaneSlice = 0` is the only valid value for non-planar formats (which is everything SDL3 GPU exposes; planar formats are NV12 etc. and aren't supported as storage textures anyway).
- It affects a real, common scenario: any SDL3 D3D12 user creating a 2D array or cubemap with compute-storage usage on a non-debug build will hit it. PBR pipelines (irradiance/prefilter cubemaps), bindless texture arrays, and most modern compute-driven renderers do this.
- The bug is **invisible in debug builds**, which is exactly the kind of bug that's most worth fixing upstream — anyone shipping a release build of an SDL3 D3D12 app will trip on it without warning.

Suggested process:
1. File an issue at https://github.com/libsdl-org/SDL describing the bug. Include:
   - Affected file/function: `src/gpu/d3d12/SDL_gpu_d3d12.c`, `D3D12_INTERNAL_CreateTexture`, the `needsUAV` block.
   - Repro: create any `SDL_GPU_TEXTURETYPE_2D_ARRAY` (or `CUBE` / `CUBE_ARRAY`) texture with `SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE` on the D3D12 backend in a release build with MSVC `/O2`.
   - Symptom: device transitions to removed, all subsequent `SDL_CreateGPU*` calls return `0x887A0001`.
   - Root cause: `uavDesc.Texture2DArray.PlaneSlice` is uninitialized; debug-build `/RTC1` zero-fill hides it.
2. Open a PR with the one-line addition. Link the issue. Mention the `/RTC1` masking behavior so reviewers understand why no one has hit it in debug-mode CI.
3. While you're there, scan the surrounding code for the same pattern — there may be other `uavDesc` / `srvDesc` / `rtvDesc` / `dsvDesc` branches with the same uninitialized-field smell. SDL3's D3D12 backend uses non-zero-init view descs throughout, so if one branch missed a field, others may too. The SRV array branches in the same function are worth eyeballing.

A small heads-up: SDL maintainers may prefer the descriptor be zero-initialized at the top (`D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {0};`) as a defense-in-depth fix that would catch any similar omission. That's a reasonable alternative the maintainers might suggest — be ready to change the PR to that style if asked.
