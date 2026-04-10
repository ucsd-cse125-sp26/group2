# AAA Rendering Pipeline — Full Implementation Plan

## Context

The game currently renders with basic Lambertian diffuse lighting — flat, no reflections, no shadows, no post-processing. The goal is to upgrade to a professional AAA-quality rendering pipeline covering PBR, shadows, IBL, SSAO, bloom, volumetrics, TAA, SSR, and subsurface scattering.

The renderer uses SDL3 GPU (Vulkan/Metal/DX12 abstraction) with GLSL → SPIR-V shaders. SDL3 GPU supports everything needed: MRT, compute shaders, HDR texture formats, cubemaps, comparison samplers, and anisotropic filtering.

**Architecture decision: Enhanced Forward rendering** (not deferred). Reasons: simpler to debug, handles transparency naturally, sufficient for moderate light counts (8–16), and avoids the G-buffer complexity. A depth pre-pass is added for early-Z and screen-space effects.

## Known Pitfalls

### UV coordinate convention (V-flip)
Vulkan expects V=0 at the **top** of the image. Models from Blender, OBJ, and many Sketchfab exports use V=0 at the **bottom** (OpenGL convention). If a model's textures appear vertically flipped — upside-down text on license plates, inverted logos, seeing the "inside" of textured parts — load it with `flipUVs=true` which applies `aiProcess_FlipUVs` (V → 1−V). Models authored natively for glTF (V=0 at top) should NOT need this flag.

### sRGB texture format
Color textures (albedo, emissive) must be uploaded as `R8G8B8A8_UNORM_SRGB` so the GPU converts sRGB→linear on sampling. Data textures (normal maps, metallic-roughness) must use `R8G8B8A8_UNORM` (linear). Getting this wrong makes dark surfaces appear washed out/white.

### Vulkan Y-flip
We flip Y in the projection matrix (`proj[1][1] *= -1`) to match Vulkan's Y-down NDC. This reverses screen-space winding, so all pipelines set `front_face = SDL_GPU_FRONTFACE_CLOCKWISE`.

## Phase Dependency Graph

```
Phase 0 (Foundation) → Phase 1 (PBR) → Phase 2 (Shadows) → Phase 10 (Volumetrics)
                         ↓                ↓
                       Phase 4 (HDR)    Phase 3 (Normal Maps + Mipmaps)
                         ↓
            ┌─────┬──────┼──────┬────────┐
          Ph5   Ph7    Ph8    Ph9      Ph11
        Skybox  SSAO  Bloom   SSR      TAA
          ↓
        Ph6 (IBL)           Ph12 (OIT)    Ph13 (SSS, optional)
```

**Critical path for max visual impact**: 0 → 1 → 4 → 5 → 6 (PBR + HDR + Skybox + IBL).

---

## Phase 0: Foundation Refactoring

**Goal**: Restructure renderer internals without changing visuals. Prepare extension points.

### Files to modify
- `src/client/renderer/Renderer.hpp/cpp` — split `drawFrame()` into `beginFrame()` / `drawScene()` / `drawModels()` / `drawUI()` / `endFrame()`. Cache swapchain color format at init.
- `src/client/renderer/ModelLoader.hpp/cpp` — extend `ModelVertex` with `tangent` (vec4, w=handedness) → 48 bytes. Add `aiProcess_CalcTangentSpace` to Assimp flags. Add PBR material data extraction (metallic/roughness factors, texture indices for normal/metallic/roughness/AO/emissive maps).
- `src/client/renderer/Camera.hpp/cpp` — add getters for near/far/fovy/aspect, add `getViewProjection()`.
- `shaders/model.vert` — add `layout(location = 3) in vec4 inTangent` (accepted but unused this phase).
- `CMakeLists.txt` — no changes yet.

### Verification
- Scene + model render identically to current state.
- `ModelVertex` is 48 bytes with tangent data populated from Assimp.

---

## Phase 1: PBR (Cook-Torrance GGX BRDF) ← START HERE

**Goal**: Replace Lambertian with physically-based rendering. Introduce material + light UBO system.

### New files
- `shaders/pbr.vert` — outputs worldPos, normal, texCoord, TBN vectors. Vertex UBO: `Matrices { model, view, projection, normalMatrix }` (256 bytes).
- `shaders/pbr.frag` — Cook-Torrance specular (GGX NDF + Smith-GGX geometry + Fresnel-Schlick) + Lambertian diffuse. Fragment UBOs: Material (slot 0, 48 bytes) + LightData (slot 1, ~432 bytes with 8 lights).

### UBO layouts
```
Material (frag UBO 0):     baseColorFactor(vec4) metallicFactor roughnessFactor aoStrength normalScale emissiveFactor(vec4) = 48B
LightData (frag UBO 1):    cameraPos(vec4) ambientColor(vec4) numLights pad[3] lights[8]×{position color params} = ~432B
Matrices (vert UBO 0):     model view projection normalMatrix = 256B
```

### Files to modify
- `src/client/renderer/Renderer.hpp/cpp` — replace `modelPipeline` with `pbrPipeline`. Push Material UBO per mesh, LightData UBO per frame. Create fallback textures: 1×1 white (albedo), 1×1 flat-normal (0.5, 0.5, 1.0), 1×1 default metallic/roughness, 1×1 white (AO), 1×1 black (emissive).
- `shaders/normal.frag` — upgrade scene geometry to use same PBR math (hardcoded material params for cube/floor).
- `CMakeLists.txt` — add pbr.vert, pbr.frag to shader list.

### Verification
- Specular highlights visible on surfaces at grazing angles.
- Roughness variation visible (smooth = sharp highlights, rough = broad response).
- Metallic surfaces show colored specular (will look dark until IBL in Phase 6 — expected).
- ImGui: toggle old Lambertian vs new PBR for comparison.

---

## Phase 2: Shadow Mapping (Cascaded Shadow Maps)

**Goal**: Directional light casts shadows via 3–4 cascade depth maps.

### New files
- `shaders/shadow.vert` — depth-only transform by light VP matrix. No fragment shader.
- `src/client/renderer/ShadowSystem.hpp` (optional) — cascade frustum splitting, light VP computation.

### Files to modify
- `src/client/renderer/Renderer.hpp/cpp` — create shadow map texture (`D32_FLOAT`, 2D_ARRAY, 2048×2048, 4 layers). Create shadow pipeline (depth-only, with depth bias). Add shadow render pass before color pass. Create comparison sampler. Make scene depth buffer also `SAMPLER`-usable.
- `shaders/pbr.frag` — add shadow map sampling: determine cascade from view-space depth, transform to light space, 3×3 PCF.

### Verification
- Shadows on floor beneath cube and model.
- Soft edges (PCF), no shadow acne, minimal peter-panning.
- ImGui: shadows on/off, cascade visualization overlay, bias adjustment.

---

## Phase 3: Normal Mapping + Mipmaps + Anisotropic Filtering

**Goal**: Surface detail via tangent-space normal maps. Sharper textures via mipmaps + anisotropy.

### Files to modify
- `shaders/pbr.frag` — add `#ifdef HAS_NORMAL_MAP` block: sample normal map, transform via TBN matrix, apply normalScale.
- `src/client/renderer/Renderer.cpp` — `uploadTexture()`: compute mip levels, create texture with all levels, generate mip chain via `SDL_BlitGPUTexture` blit chain. Update sampler: `mipmapMode = LINEAR`, `enable_anisotropy = true`, `max_anisotropy = 8`.
- `src/client/renderer/ModelLoader.cpp` — extract `aiTextureType_NORMALS` textures from Assimp.

### Verification
- Oblique textures sharper (anisotropic). No distant shimmering (mipmaps).
- Normal-mapped surfaces show detail up close. Flat fallback produces identical results where no normal map exists.

---

## Phase 4: HDR + Tone Mapping + Gamma Correction

**Goal**: Render to float buffer, apply tone mapping and gamma in a fullscreen post-process.

### New files
- `shaders/fullscreen.vert` — generates fullscreen triangle from `gl_VertexIndex` (3 vertices, no VBO).
- `shaders/tonemap.frag` — samples HDR texture, applies exposure, ACES filmic tone mapping, gamma 2.2.

### Files to modify
- `src/client/renderer/Renderer.hpp/cpp` — add `hdrTarget` (`R16G16B16A16_FLOAT`, `COLOR_TARGET | SAMPLER`). Add `tonemapPipeline`. Render flow becomes: shadow pass → color pass (to HDR) → tonemap pass (to swapchain) → ImGui (to swapchain). Add `TonemapParams` UBO (exposure, gamma, mode).
- `CMakeLists.txt` — add fullscreen.vert, tonemap.frag.

### Verification
- Bright highlights don't clip. Dark areas have correct contrast.
- ImGui: exposure slider, tone mapping operator selector (ACES/Reinhard), toggle gamma.

---

## Phase 5: Skybox

**Goal**: Cubemap environment background.

### New files
- `shaders/skybox.vert` — hardcoded cube vertices, view matrix with translation removed.
- `shaders/skybox.frag` — samples `samplerCube` using interpolated direction.

### Files to modify
- `src/client/renderer/Renderer.hpp/cpp` — add skybox pipeline (depth test LESS_OR_EQUAL, depth write off). Load cubemap texture (`SDL_GPU_TEXTURETYPE_CUBE`, 6 layers). Draw skybox first in color pass (or last with depth ≤). Remove hardcoded clear color.
- `CMakeLists.txt` — add skybox.vert, skybox.frag.

### Verification
- Sky visible in all directions. Rotates with camera, doesn't translate. No face seams.
- Responds to exposure control (HDR sky).

---

## Phase 6: Image-Based Lighting (IBL)

**Goal**: Environment reflections + diffuse irradiance from the skybox cubemap.

### New resources to generate (offline tool or compute shader)
- Pre-filtered environment map (cubemap, multiple mip levels for roughness).
- Diffuse irradiance map (low-res cubemap, ~32×32 per face).
- BRDF LUT (512×512, `RG16_FLOAT`, precomputed split-sum).

### Files to modify
- `shaders/pbr.frag` — add IBL: `irradianceMap` (diffuse), `prefilterMap` (specular, mipmapped), `brdfLUT`. Compute `fresnelSchlickRoughness`, sample maps, blend with direct lighting.
- `src/client/renderer/Renderer.hpp/cpp` — load/generate IBL textures. Bind as additional fragment samplers. Update `loadShader` sampler count (up to 7).

### Verification
- Metallic surfaces reflect environment. Rough surfaces show blurred reflections. "Dark metallic" issue from Phase 1 gone.
- ImGui: IBL on/off, IBL intensity.

---

## Phase 7: SSAO (Compute-Based)

**Goal**: Contact shadows and ambient darkening in crevices.

### New files
- `shaders/ssao.comp` — 64-sample hemisphere kernel, reconstruct view-space position from depth, check occlusion.
- `shaders/ssao_blur.comp` — bilateral blur (4×4) preserving edges.

### Files to modify
- `src/client/renderer/Renderer.hpp/cpp` — SSAO compute pipelines. SSAO textures (`R8_UNORM`, screen-res). Dispatch after color pass. Pass SSAO UBO (projection, invProjection, kernel[64], radius, bias).
- `shaders/tonemap.frag` — multiply blurred SSAO into ambient term.

### Verification
- Cube-floor junction visibly darker. Open areas unaffected. No banding.
- ImGui: SSAO on/off, radius slider, raw SSAO visualization.

---

## Phase 8: Bloom (Compute-Based)

**Goal**: Bright areas glow with multi-scale light bleed.

### New files
- `shaders/bloom_threshold.comp` — extract bright pixels (luminance > threshold) to half-res.
- `shaders/bloom_downsample.comp` — progressive downsampling (6–7 levels), Karis average on first level.
- `shaders/bloom_upsample.comp` — progressive upsampling with tent filter + additive blend.

### Files to modify
- `src/client/renderer/Renderer.hpp/cpp` — bloom mip chain textures (`R16G16B16A16_FLOAT`). Dispatch: threshold → downsample ×6 → upsample ×6.
- `shaders/tonemap.frag` — add bloom texture, `color += bloomStrength * bloom` before tonemapping.

### Verification
- Specular highlights and emissive surfaces glow. Soft multi-scale. No fireflies.
- ImGui: bloom on/off, intensity, threshold.

---

## Phase 9: Screen-Space Reflections (SSR)

**Goal**: Real-time reflections via screen-space ray marching.

### New files
- `shaders/ssr.comp` — per-pixel ray march along reflection vector through depth buffer.

### Files to modify
- `src/client/renderer/Renderer.hpp/cpp` — SSR compute pipeline, SSR texture (`R16G16B16A16_FLOAT`).
- `shaders/tonemap.frag` or composite pass — blend SSR with IBL specular. Roughness-based fade (SSR for roughness < 0.3).

### Verification
- Smooth floors reflect nearby objects. Fades at screen edges. Rough surfaces unaffected.

---

## Phase 10: Volumetric Lighting / Fog

**Goal**: Light shafts from directional lights + atmospheric distance fog.

### New files
- `shaders/volumetric.comp` — ray march camera→pixel, sample shadow map at each step, Henyey-Greenstein phase function.
- `shaders/volumetric_blur.comp` — bilateral blur on half-res buffer.

### Files to modify
- `src/client/renderer/Renderer.hpp/cpp` — volumetric compute pipeline, half-res buffer. Composite before tonemapping.

### Verification
- Light shafts through shadow-casting geometry. Distance fog. No banding (blue noise dithering).

---

## Phase 11: Temporal Anti-Aliasing (TAA)

**Goal**: Smooth jagged edges via temporal accumulation with sub-pixel jitter.

### New files
- `shaders/motion_vectors.comp` — compute screen-space motion from depth + previous/current VP.
- `shaders/taa.comp` — reproject history, neighborhood clamp, blend 10% current + 90% history.

### Files to modify
- `src/client/renderer/Camera.hpp/cpp` — store previous VP matrix.
- `src/client/renderer/Renderer.hpp/cpp` — add Halton jitter to projection. Motion vector texture, history ping-pong buffers. TAA dispatch after tonemapping.

### Verification
- Edges visibly smoother. No ghosting on movement. Static scenes converge sharp.

---

## Phase 12: Order-Independent Transparency (OIT)

**Goal**: Correct rendering of overlapping transparent surfaces (glass windows seen through other glass, bottles, particles) without draw-order artifacts.

**Current state**: Simple forward alpha blending with a separate transparent pipeline (opaque pass → skybox → transparent pass). Works for single-layer transparency (glass against sky) but fails for multi-layer (looking through two windows — the far window appears opaque because it was rendered before the near one blended on top).

### Approach: Weighted Blended OIT (McGuire & Bavoil 2013)
The simplest OIT method that doesn't require per-pixel linked lists. Two extra render targets during the transparent pass:
- **Accumulation buffer** (RGBA16F): `sum(color_i * weight_i * alpha_i)`
- **Revealage buffer** (R8): `product(1 - alpha_i)`

The weight function biases by depth: `weight = alpha * max(0.01, 3000 * (1-gl_FragCoord.z)^3)`.

A fullscreen resolve pass composites: `final = accum.rgb / max(accum.a, 0.00001)` blended with the opaque+sky background using the revealage value.

### New files
- `shaders/pbr_wboit.frag` — variant of pbr.frag that writes to the two OIT buffers instead of blending directly.
- `shaders/oit_resolve.frag` — fullscreen pass that composites the OIT buffers onto the opaque background.

### Files to modify
- `src/client/renderer/Renderer.hpp/cpp` — add OIT render targets (accumulation + revealage), OIT pipeline (additive blending for accum, multiplicative for revealage), resolve pipeline.

### Verification
- Looking through two glass windows shows the far window correctly.
- Bottle is fully visible against all backgrounds (sky, floor, other objects).
- No draw-order artifacts.

### Alternative: Per-Pixel Linked Lists
More accurate but requires compute shaders + atomic operations + storage buffers. Higher complexity, better results for many overlapping layers. Consider if WBOIT has visible artifacts.

---

## Phase 13: Subsurface Scattering (Optional Stretch)

**Goal**: Translucent skin/organic look for character models.

### Approach
Screen-space separable Gaussian diffusion, weighted by material SSS flag + depth similarity.

---

## Complexity Summary

| Phase | Feature | Est. Days | Risk |
|-------|---------|-----------|------|
| 0 | Foundation | 2–3 | Low |
| 1 | **PBR** | 4–5 | Medium |
| 2 | Shadows (CSM) | 4–5 | High |
| 3 | Normal maps + mipmaps | 2–3 | Low |
| 4 | HDR + tonemapping | 2–3 | Low |
| 5 | Skybox | 1–2 | Low |
| 6 | IBL | 3–4 | Medium |
| 7 | SSAO | 3–4 | Medium |
| 8 | Bloom | 2–3 | Low |
| 9 | SSR | 4–5 | High |
| 10 | Volumetrics | 3–4 | High |
| 11 | TAA | 3–4 | Medium |
| 12 | OIT (transparency) | 2–3 | Medium |
| 13 | SSS | 3–4 | Medium |
| | **Total** | **38–51** | |

**Recommended milestone**: Phases 0–6 (Foundation through IBL) = dramatic visual upgrade in ~3–4 weeks. Phases 7–8 (SSAO + Bloom) as next tier. Phase 12 (OIT) if transparency artifacts are a priority. Phases 9–11, 13 as stretch goals.
