# Graphics / Rendering Pipeline

Reference documentation for the forward PBR renderer.  
Source files live under `src/client/renderer/`.

---

## 1. Architecture Overview

The renderer (`Renderer` class in `Renderer.hpp` / `Renderer.cpp`) is a **forward PBR pipeline** built on the **SDL3 GPU API**.  SDL3 GPU is a cross-platform abstraction that targets Vulkan, Metal, and Direct3D 12 via a single API surface -- the renderer selects SPIR-V or MSL shaders at init time depending on what the driver supports.

Key design points:

- **Single command buffer per frame** -- all geometry passes, compute dispatches, and the final tonemap pass are recorded into one `SDL_GPUCommandBuffer` acquired at the top of `drawFrame()` and submitted at the bottom.
- **HDR intermediate** -- the main colour pass renders into an `RGBA16F` render target (`hdrTarget`).  All lighting, IBL, and emissive output stays in linear HDR until the final tone-mapping pass converts to the LDR swapchain format.
- **Cascaded shadow maps** -- a 4-cascade directional light shadow atlas provides shadow coverage up to a configurable distance (`shadowDistance`, default 3000 world units).
- **Compute post-processing** -- SSAO, bloom, SSR, volumetric lighting, motion vectors, and TAA all run as compute shader dispatches between the HDR colour pass and the tonemap pass.
- **ImGui overlay** -- rendered in the LDR tonemap pass, on top of the tone-mapped image.

### File Map

| File | Role |
|------|------|
| `src/client/renderer/Renderer.hpp` | Class declaration, render-target / pipeline members, public API |
| `src/client/renderer/Renderer.cpp` | All pipeline creation, `init()`, `drawFrame()`, IBL generation, model upload |
| `src/client/renderer/Camera.hpp/cpp` | Perspective camera with lookAt, Y-flip for Vulkan NDC |
| `src/client/renderer/ModelLoader.hpp` | `ModelVertex`, `LoadedModel`, `MaterialData` structs; `loadModel()` declaration |
| `src/client/renderer/ShaderUtils.hpp` | Shared `loadShader()` utility (used by both Renderer and ParticleSystem) |
| `src/client/animation/SkinnedModel.hpp` | Skinned mesh animation -- CPU-side vertex skinning, deferred GPU re-upload |
| `src/client/particles/ParticleSystem.hpp` | GPU particle system -- renders inside the HDR pass |
| `shaders/` | GLSL shader sources (compiled to SPIR-V at build time) |

---

## 2. Render Pass Architecture

Every frame flows through `drawFrame()`, which records all work into a single command buffer.  The passes execute in the following order:

### Pass 0: Cascaded Shadow Maps (Depth-Only)

**Purpose:** Render scene depth from the directional light's perspective into a shadow atlas.

- **Atlas texture:** `shadowMap` -- `D32_FLOAT`, `(2 * k_shadowMapSize)^2` = 4096x4096 pixels.
- **Layout:** 2x2 grid of quadrants, each `k_shadowMapSize` (2048) pixels per side.  Cascade `c` occupies quadrant `(c%2, c/2)`.
- **Cascade splitting:** Uses the practical split scheme (blend of logarithmic and linear, controlled by `cascadeLambda`).  Split distances range from `camera.getNear()` to `shadowDistance`.
- **Light projection:** Orthographic, RH, zero-to-one depth.  The AABB of each sub-frustum is computed in light space, then texel-snapped to prevent shadow swimming on camera movement.
- **Caster padding:** 2000 units behind the frustum and 500 units in front to catch off-screen casters.
- **Depth bias:** Constant factor 0.75, slope factor 1.0 (set on the rasterizer state).
- **Culling:** Disabled (`CULLMODE_NONE`) because the camera Y-flip reverses winding -- disabling culling is the standard fix for shadow passes.

Within each cascade's viewport/scissor:
1. **Scene models** (Assimp-loaded, `drawInScenePass == true`) -- uses `shadowPipeline` with `shadow.vert` / `shadow.frag`.  Only opaque meshes.
2. **Entity models** (ECS-driven via `entityRenderCmds`) -- same pipeline, using each entity's `worldTransform`.
3. **Scene geometry** (procedural boxes + floor) -- uses `sceneShadowPipeline` with `projective.vert` / `shadow.frag`.

### Pass 1: Main HDR Colour Pass

**Render target:** `hdrTarget` (`RGBA16F`, screen resolution).  
**Depth buffer:** `depthTexture` (`D32_FLOAT`, stored for post-processing reads).

All sub-passes share a single `SDL_GPURenderPass`.  Draw order matters for correctness:

#### 1a. Scene Geometry

- **Pipeline:** `scenePipeline` (`projective.vert` + `normal.frag`)
- **Geometry:** Hard-coded cube + floor -- 1182 vertices generated procedurally by the vertex shader from `gl_VertexIndex` (no vertex buffer).
- **Lighting:** Cascade shadow sampling, directional sun + fill light, ambient colour.
- **Toggle:** `toggles.sceneGeometry`

#### 1b. PBR Models -- Opaque Pass

- **Pipeline:** `pbrPipeline` (`pbr.vert` + `pbr.frag`)
- **Vertex layout:** `ModelVertex` (48 bytes): position(vec3) + normal(vec3) + texCoord(vec2) + tangent(vec4).
- **Textures bound (8 samplers):** albedo, metallic-roughness, emissive, normal map, irradiance cubemap, prefilter cubemap, BRDF LUT, shadow map.
- **UBOs:** `Matrices` (vertex), `MaterialUBO` (fragment slot 0), `LightDataUBO` (fragment slot 1), `ShadowDataFragUBO` (fragment slot 2).
- **Depth:** Test + write enabled.
- **Culling:** Disabled (GLB models may be double-sided).
- **Toggle:** `toggles.pbrModels`

#### 1c. Skybox

Rendered **after opaques but before transparents** so that transparent fragments blend against the sky colour rather than black.

- **Pipeline:** `skyboxPipeline` (`skybox.vert` + `skybox.frag`)
- **Depth:** Test enabled (`LESS_OR_EQUAL` to fill at depth=1.0), write disabled.
- **Modes:** Procedural gradient sky or HDR cubemap (`envCubemap`), selected by `useHDRSkybox` flag.
- **Geometry:** 36-vertex cube (hardcoded in shader).
- **UBO:** `SkyboxMatricesUBO` with view rotation (translation zeroed) + projection.  Fragment UBO selects cubemap vs procedural + sun direction.
- **Toggle:** `toggles.skybox`

#### 1d. Entity Models (ECS-Driven)

- **Pipeline:** Reuses `pbrPipeline` (same shaders, same 8 samplers).
- **Source:** `entityRenderCmds` vector, set each frame by `Game` via `setEntityRenderList()`.
- **Transform:** Each `EntityRenderCmd` carries a full `worldTransform` (position x rotation x scale).
- **Only opaque meshes** are drawn here; transparent entity meshes are not yet rendered.
- **Toggle:** `toggles.entityModels`

#### 1e. PBR Models -- Transparent Pass

- **Pipeline:** `pbrTransparentPipeline` (same `pbr.vert` + `pbr.frag`, different blend state).
- **Blending:** `SRC_ALPHA / ONE_MINUS_SRC_ALPHA` (standard alpha blend).
- **Depth:** Test enabled, **write disabled** -- transparent surfaces do not occlude.
- **Toggle:** `toggles.pbrModels`

#### 1f. Particle System

- **Rendered after opaques + skybox**, inside the HDR pass.
- **Integration:** `ParticleSystem::uploadToGpu(cmd)` is called before any render pass; `ParticleSystem::render(pass, cmd)` draws inside the colour pass.
- **Uniform data:** `ParticleUniforms` struct is pushed to vertex slot 0 (view, proj, camPos, camRight, camUp).
- **Toggle:** `toggles.particles`

#### 1g. Weapon Viewmodel

- **Pipeline:** Reuses `pbrPipeline`.
- **Rendered last** in the HDR pass.  Depth is NOT cleared -- the weapon draws over the scene at close range.
- **Source:** `weaponVM` struct set each frame by `Game` via `setWeaponViewmodel()`.
- **Shadow:** Disabled for the viewmodel (`shadowMapSize = 0` in the shadow UBO).
- **Toggle:** `toggles.weaponViewmodel`

### Compute Passes (Between HDR and Tonemap)

All compute shaders use 16x16 workgroups and dispatch `ceil(width/16) x ceil(height/16)` thread groups.

#### SSAO (Ground Truth Ambient Occlusion)

1. **GTAO main pass** (`gtao.comp`): Reads depth texture, writes `ssaoTexture` (R8_UNORM, screen-res).  Parameters: radius=40, falloffExp=2, 3 slices, 6 steps.
2. **Bilateral blur** (`gtao_blur.comp`): Reads raw AO + depth, writes `ssaoBlurTexture` (R8_UNORM).

#### Bloom (Downsample + Upsample Chain)

- **Mip chain:** `k_bloomMips = 6` levels, each half the size of the previous, RGBA16F.
- **Downsample** (`bloom_downsample.comp`): First pass reads `hdrTarget`; subsequent passes read the previous mip.  A `isFirstPass` flag distinguishes the HDR source from intermediate mips.
- **Upsample** (`bloom_upsample.comp`): Walks the chain back up from smallest to largest, additively blending each level with intensity 0.3.

#### SSR (Screen-Space Reflections)

- **Shader:** `ssr.comp`
- **Inputs:** HDR colour, depth, previous-frame SSR (temporal accumulation), motion vectors.
- **Output:** `ssrTexture[ssrDst]` (RGBA16F, ping-pong between two textures).
- **Parameters:** maxDist=500, thickness=5, jitterStrength=0.06.
- **Modes:** 0=Sharp, 1=Stochastic, 2=Masked (default, exposed as `ssrMode`).

#### Volumetric Lighting

- **Shader:** `volumetric.comp`
- **Resolution:** Half-res (`w/2 x h/2`), RGBA16F.
- **Inputs:** Depth texture + shadow map (with comparison sampler).
- **Parameters:** fogDensity=0.0002, Mie scattering G=0.7, maxDistance=2000.
- **Uses cascade shadow data** for shadowed volumetric scattering.

#### Motion Vectors

- **Shader:** `motion_vectors.comp`
- **Output:** `motionVectorTexture` (RG16_FLOAT, screen-res).
- **Inputs:** Depth texture, current inverse VP, previous frame's VP.

#### TAA (Temporal Anti-Aliasing)

- **Shader:** `taa.comp`
- **Inputs:** Current HDR colour, previous TAA history, motion vectors.
- **Output:** `taaHistory[dstIdx]` (RGBA16F, ping-pong).
- **Blend factor:** 0.1 (90% history, 10% current frame).

### Pass 2: Tone Mapping + ImGui Overlay

**Pipeline:** `tonemapPipeline` (`fullscreen.vert` + `tonemap.frag`)

- **Input textures (5 samplers):** HDR colour, bloom mip 0, SSAO blur, SSR (current ping-pong), volumetric.
- **UBO (`TonemapParamsUBO`):** exposure (1.0), gamma (2.2), tonemapMode (0 = ACES), plus per-effect strength multipliers (bloom, SSAO, SSR, volumetric, sharpen).
- **Geometry:** Fullscreen triangle (3 vertices, no vertex buffer).
- **Output:** LDR swapchain texture (or `captureRT` if a screenshot is pending).
- **ImGui:** `ImGui_ImplSDLGPU3_RenderDrawData()` is called inside this render pass, drawing the debug UI on top of the tone-mapped image.

---

## 3. Pipeline Objects

### Graphics Pipelines

| Pipeline | Shaders | Purpose | Depth | Blending |
|----------|---------|---------|-------|----------|
| `scenePipeline` | `projective.vert` + `normal.frag` | Hard-coded cube/floor (procedural geometry) | Test+Write, LESS | None |
| `pbrPipeline` | `pbr.vert` + `pbr.frag` | Assimp model opaque meshes, entity models, weapon | Test+Write, LESS | None |
| `pbrTransparentPipeline` | `pbr.vert` + `pbr.frag` | Assimp model transparent meshes | Test only, LESS | SRC_ALPHA / ONE_MINUS_SRC_ALPHA |
| `skyboxPipeline` | `skybox.vert` + `skybox.frag` | Procedural/cubemap skybox | Test only, LESS_OR_EQUAL | None |
| `tonemapPipeline` | `fullscreen.vert` + `tonemap.frag` | HDR-to-LDR fullscreen pass | None | None |
| `shadowPipeline` | `shadow.vert` + `shadow.frag` | Depth-only shadow map (Assimp models) | Test+Write, LESS | N/A (no colour) |
| `sceneShadowPipeline` | `projective.vert` + `shadow.frag` | Depth-only shadow map (procedural scene geometry) | Test+Write, LESS | N/A (no colour) |

All pipelines use `PRIMITIVETYPE_TRIANGLELIST`.  The scene and PBR pipelines render into `RGBA16F`.  The tonemap pipeline renders into the swapchain format.  Shadow pipelines have zero colour targets.

Front face is set to `CLOCKWISE` on scene/PBR pipelines to compensate for the Vulkan Y-flip in the projection matrix.

### Compute Pipelines

| Pipeline | Shader | Workgroup | Samplers | RW Textures | UBOs |
|----------|--------|-----------|----------|-------------|------|
| `bloomDownsamplePipeline` | `bloom_downsample.comp` | 16x16x1 | 1 | 1 | 1 |
| `bloomUpsamplePipeline` | `bloom_upsample.comp` | 16x16x1 | 1 | 1 | 1 |
| `ssaoPipeline` | `gtao.comp` | 16x16x1 | 1 | 1 | 1 |
| `ssaoBlurPipeline` | `gtao_blur.comp` | 16x16x1 | 2 | 1 | 1 |
| `ssrPipeline` | `ssr.comp` | 16x16x1 | 4 | 1 | 1 |
| `volumetricPipeline` | `volumetric.comp` | 16x16x1 | 2 | 1 | 1 |
| `motionVectorPipeline` | `motion_vectors.comp` | 16x16x1 | 1 | 1 | 1 |
| `taaPipeline` | `taa.comp` | 16x16x1 | 3 | 1 | 1 |

---

## 4. Render Targets and Textures

### Core Render Targets

| Texture | Format | Resolution | Usage |
|---------|--------|------------|-------|
| `depthTexture` | `D32_FLOAT` | Screen-res | Scene depth; read by SSAO, SSR, volumetrics, motion vectors |
| `hdrTarget` | `RGBA16F` | Screen-res | Main HDR colour output; read by bloom, SSR, TAA, tonemap |
| `shadowMap` | `D32_FLOAT` | 4096x4096 | 4-cascade shadow atlas (2x2 grid of 2048x2048 quadrants) |

### Post-Processing Textures

| Texture | Format | Resolution | Purpose |
|---------|--------|------------|---------|
| `bloomMips[0..5]` | `RGBA16F` | Half-res, progressive halving | 6-level bloom downsample/upsample chain |
| `ssaoTexture` | `R8_UNORM` | Screen-res | Raw GTAO output |
| `ssaoBlurTexture` | `R8_UNORM` | Screen-res | Bilateral-blurred SSAO (fed to tonemap) |
| `ssrTexture[0..1]` | `RGBA16F` | Screen-res | SSR ping-pong pair for temporal accumulation |
| `volumetricTexture` | `RGBA16F` | Half-res (`w/2 x h/2`) | Volumetric light scattering |
| `motionVectorTexture` | `RG16_FLOAT` | Screen-res | Per-pixel screen-space motion vectors |
| `taaHistory[0..1]` | `RGBA16F` | Screen-res | TAA ping-pong history buffers |

### IBL Textures

| Texture | Format | Resolution | Purpose |
|---------|--------|------------|---------|
| `brdfLUT` | `RG16_FLOAT` | 512x512 | Split-sum BRDF lookup table |
| `irradianceMap` | `RGBA16F` cubemap | 32x32 per face | Diffuse IBL (cosine-weighted hemisphere integral) |
| `prefilterMap` | `RGBA16F` cubemap | 128x128 per face, 5 mip levels | Specular IBL (roughness-filtered environment) |
| `envCubemap` | `RGBA16F` cubemap | 512x512 per face | HDR environment map for skybox rendering |

### Fallback Textures (1x1)

| Texture | Colour | sRGB | Used For |
|---------|--------|------|----------|
| `fallbackWhite` | (255,255,255,255) | Yes | Missing albedo / AO |
| `fallbackFlatNormal` | (128,128,255,255) | No | Missing normal map (tangent-space up) |
| `fallbackMR` | (0,128,0,255) | No | Missing metallic-roughness (metal=0, rough=0.5) |
| `fallbackBlack` | (0,0,0,255) | Yes | Missing emissive / disabled post-process input |

### Samplers

| Sampler | Filter | Address Mode | Notes |
|---------|--------|--------------|-------|
| `pbrSampler` | Linear, trilinear mips | Repeat | Anisotropy 8x, max LOD 13 |
| `shadowSampler` | Linear | Clamp-to-edge | Comparison mode (LESS_OR_EQUAL) for PCF |
| `tonemapSampler` | Linear, nearest mips | Clamp-to-edge | Used for fullscreen passes and compute reads |
| `iblSampler` | Linear, trilinear mips | Clamp-to-edge | Max LOD 5 (prefilter map mips) |

---

## 5. IBL (Image-Based Lighting)

IBL provides ambient specular and diffuse lighting from the environment.  Three textures are generated at init time (and regenerated when an HDR skybox is loaded):

### BRDF LUT (`brdfLUT`)

- **Size:** 512x512, `RG16_FLOAT`.
- **Content:** Split-sum approximation of the Cook-Torrance BRDF integral.  X axis = NdotV, Y axis = roughness.  R channel = scale, G channel = bias.
- **Generation:** CPU-side Monte Carlo integration (256 samples per texel) using importance-sampled GGX and Smith-GGX geometry with IBL remapping (`k = rough^2 / 2`).  The Hammersley low-discrepancy sequence provides the sample distribution.
- **Upload:** Float-to-half conversion, uploaded via transfer buffer.

### Irradiance Map (`irradianceMap`)

- **Size:** 32x32 per face, 6-face cubemap, `RGBA16F`.
- **Content:** Cosine-weighted hemisphere integral of the environment for diffuse IBL.
- **Generation (procedural):** For each texel direction, ~1000 hemisphere samples weighted by `cos(theta) * sin(theta)`.  The procedural sky function (matching `skybox.frag`) is sampled with 4x brightness boost to approximate outdoor environment levels.
- **Generation (HDR):** When an HDR skybox is loaded, the irradiance map is regenerated by sampling the loaded cubemap data (`cubeFaces[]`) instead of the procedural sky.

### Prefilter Map (`prefilterMap`)

- **Size:** 128x128 per face, 5 mip levels (128, 64, 32, 16, 8), 6-face cubemap, `RGBA16F`.
- **Content:** Roughness-filtered environment for specular IBL.  Mip 0 = mirror reflection, mip 4 = fully rough.
- **Generation:** Cone sampling with roughness-dependent lobe width.  Mip 0 uses a single sample (perfect mirror); higher mips use 64 quasi-random samples.
- **Roughness mapping:** `roughness = mip / 4.0`.

### Shader Usage

In `pbr.frag`, the IBL contribution is computed as:
- **Diffuse:** `irradianceMap` sampled at the surface normal direction, multiplied by `(1 - metallic) * albedo`.
- **Specular:** `prefilterMap` sampled at the reflection vector (mip selected by roughness), combined with `brdfLUT` lookup at `(NdotV, roughness)` to apply the Fresnel-weighted split-sum.

---

## 6. HDR Skybox System

### Loading Flow

1. **Scan:** `scanHDRFiles()` finds `.hdr` and `.exr` files in `assets/uploads_files_812442_HdriFree/`.
2. **Load:** `loadHDRSkybox(path)` reads the equirectangular HDR image via `stbi_loadf()` (3-channel float).
3. **Cubemap conversion:** For each of the 6 cube faces (512x512), each texel's direction is computed and bilinearly sampled from the equirectangular source.  Pixel data is converted to float16 (`RGBA16F`) and uploaded face-by-face via transfer buffers.
4. **IBL regeneration:** The irradiance map and prefilter map are fully regenerated from the loaded cubemap data (CPU-side hemisphere integration using the float cubemap as the light source).
5. **State update:** `useHDRSkybox = true`, `currentHDRName` set to the file stem.

### Live Switching

The ImGui debug UI exposes `availableHDRFiles` as a dropdown.  Selecting a different file calls `loadHDRSkybox()`, which releases the old `envCubemap` texture, creates a new one, and rebuilds all IBL maps.  Selecting "(procedural)" sets `useHDRSkybox = false`.

### Cubemap Face Order

| Face | Index | Major Axis Direction |
|------|-------|---------------------|
| +X | 0 | `(1, -v, -u)` |
| -X | 1 | `(-1, -v, u)` |
| +Y | 2 | `(u, 1, v)` |
| -Y | 3 | `(u, -1, -v)` |
| +Z | 4 | `(u, -v, 1)` |
| -Z | 5 | `(-u, -v, -1)` |

---

## 7. Model Loading Pipeline

### CPU Side (`ModelLoader.hpp` / Assimp)

1. **Load:** `loadModel(path, outModel, flipUVs)` uses Assimp to parse GLB/OBJ/FBX files.
2. **Scene graph traversal:** Per-node transforms are baked into vertex positions.
3. **Vertex extraction:** Each vertex is packed into `ModelVertex` (48 bytes):
   - `position` (vec3, offset 0)
   - `normal` (vec3, offset 12)
   - `texCoord` (vec2, offset 24)
   - `tangent` (vec4, offset 32) -- xyz = tangent direction, w = bitangent handedness
4. **Material extraction:** `MaterialData` captures glTF PBR properties:
   - `baseColorFactor` (vec4), `metallicFactor`, `roughnessFactor`, `aoStrength`, `normalScale`
   - `emissiveFactor` (vec4)
   - `alphaMode` (Opaque / Mask / Blend), `alphaCutoff`
5. **Texture decoding:** Embedded PNG/JPEG textures are decoded to RGBA pixel arrays.  Each `TextureData` carries an `isSRGB` flag (true for colour textures, false for data textures like normal/MR maps).

### GPU Upload (`Renderer::uploadModel`)

1. **Textures:** Each `TextureData` is uploaded via `uploadTexture()`:
   - Format: `R8G8B8A8_UNORM_SRGB` (colour) or `R8G8B8A8_UNORM` (data).
   - Full mip chain generated via GPU blit (linear filter downsample, one blit per mip level).
2. **Geometry:** All meshes are packed into a single transfer buffer (vertex + index data interleaved per mesh), then uploaded in one copy pass.
3. **GPU buffers:** Separate `SDL_GPUBuffer` per mesh for vertex and index data.
4. **Material binding:** Each `GpuMesh` stores texture indices and material data; the `resolveTex()` lambda maps indices to GPU textures at draw time, falling back to the appropriate 1x1 default.

### Public API

| Method | Description |
|--------|-------------|
| `loadSceneModel(filename, pos, scale, flipUVs)` | Load from `assets/`, set `drawInScenePass = false` (entity/weapon only), return model index. |
| `uploadSceneModel(model)` | Upload a pre-built `LoadedModel` (e.g. from `SkinnedModel`), return model index. |

---

## 8. Skinned Animation Integration

`SkinnedModel` (`src/client/animation/SkinnedModel.hpp`) provides CPU-side skeletal animation:

### Workflow

1. **Load:** `SkinnedModel::load(path)` parses FBX via Assimp, extracts skeleton + skin weights + animation, converts to ozz-animation runtime format.
2. **Initial upload:** `SkinnedModel::getLoadedModel()` returns a `LoadedModel` with first-frame vertex data.  The caller passes this to `Renderer::uploadSceneModel()` to get a model index.
3. **Per-frame update:** `SkinnedModel::update(dt)` advances the animation clock, samples the skeleton pose, and rewrites vertex positions/normals via linear-blend skinning on the CPU.
4. **Deferred GPU re-upload:** The caller retrieves `getSkinnedVertices(meshIdx)` and calls `Renderer::updateModelMeshVertices(modelIndex, meshIndex, vertices, count)`.

### Deferred Upload Mechanism

`updateModelMeshVertices()` does **not** immediately submit GPU work.  Instead:

1. The vertex data is copied into a `PendingVertexUpload` struct (destination buffer pointer + byte vector).
2. At the start of the next `drawFrame()`, all pending uploads are flushed:
   - A persistent `skinTransferBuf` (transfer buffer) is reused with `cycle=true` so the GPU can still read from the previous frame's copy without stalling.
   - Each upload maps the transfer buffer, copies data, and records a `SDL_UploadToGPUBuffer` in the frame's copy pass.
3. **Zero extra command buffer submissions, zero pipeline stalls.**

---

## 9. Entity Rendering

### Data Flow

```
Game::iterate()
    |
    |-- For each entity with Renderable + Position:
    |       Build EntityRenderCmd { modelIndex, worldTransform }
    |
    |-- renderer.setEntityRenderList(cmds)
    |-- renderer.setWeaponViewmodel(vm)
    |
    v
Renderer::drawFrame()
    |
    |-- Shadow pass: Draw entity models into shadow atlas
    |-- Colour pass: Draw entity models with PBR pipeline
    |-- Colour pass: Draw weapon viewmodel (last, no depth clear)
```

### EntityRenderCmd

```cpp
struct EntityRenderCmd {
    int32_t modelIndex;         // Index into Renderer::models[]
    glm::mat4 worldTransform;   // Full world transform (position * rotation * scale)
};
```

The model index is obtained at load time via `loadSceneModel()` or `uploadSceneModel()`.  The world transform is computed from the entity's ECS Position + Rotation + Scale components.

### WeaponViewmodel

```cpp
struct WeaponViewmodel {
    int32_t modelIndex;    // Index into Renderer::models[]
    glm::mat4 transform;   // Transform in viewmodel space (relative to camera)
    bool visible;
};
```

The weapon viewmodel is rendered last in the HDR pass.  It has its own lighting setup (no shadows, slightly different fill light) and does not clear depth so it draws over the scene.

---

## 10. Render Toggles

The `RenderToggles` struct provides live toggles for every render system.  All default to `true`.  The Renderer checks these each frame and skips the corresponding pass or dispatch when disabled.

```cpp
struct RenderToggles {
    // Geometry
    bool sceneGeometry;    // Hard-coded cube + floor
    bool pbrModels;        // Assimp-loaded scene models (opaque + transparent)
    bool entityModels;     // ECS-driven entity models (Renderable component)
    bool weaponViewmodel;  // First-person weapon
    bool skybox;           // Procedural / cubemap skybox

    // Shadow
    bool shadows;          // Shadow map pass + shadow sampling in PBR

    // Post-processing
    bool ssao;             // Screen-space ambient occlusion
    bool bloom;            // Bloom downsample + upsample chain
    bool ssr;              // Screen-space reflections
    bool volumetrics;      // Volumetric lighting / god rays
    bool taa;              // Temporal anti-aliasing
    bool tonemap;          // HDR -> LDR tone mapping (disabling = raw HDR blit)

    // Effects
    bool particles;        // GPU particle system
    bool sdfText;          // SDF text rendering (HUD + world)
};
```

When a post-processing toggle is disabled, the tonemap shader receives a fallback texture (`fallbackBlack` for additive effects, `fallbackWhite` for multiplicative SSAO) and the corresponding strength parameter is set to 0.

---

## 11. Live-Tunable Parameters

All of these are public members of `Renderer`, exposed to ImGui for real-time adjustment:

### Sun / Lighting

| Parameter | Default | Description |
|-----------|---------|-------------|
| `sunAzimuth` | 210.0 | Degrees, 0=North, 90=East, 180=South |
| `sunElevation` | 60.0 | Degrees above horizon |
| `sunIntensity` | 3.0 | Primary directional light intensity |
| `fillIntensity` | 0.8 | Fill/bounce light intensity |
| `ambientR/G/B` | 0.08/0.09/0.12 | PBR ambient colour (dark blue tint) |

Sun direction is computed from azimuth/elevation via `getSunDirection()`:
```
dir = (cos(el)*sin(az), sin(el), cos(el)*cos(az))
```

### Post-Processing Strengths

| Parameter | Default | Description |
|-----------|---------|-------------|
| `bloomStr` | 0.04 | Bloom compositing strength |
| `ssaoStr` | 0.8 | SSAO compositing strength |
| `ssrStr` | 0.4 | SSR compositing strength |
| `volStr` | 0.15 | Volumetric compositing strength |
| `sharpenStr` | 0.6 | Post-TAA CAS sharpening strength |

### Shadow Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `shadowBiasVal` | 0.0005 | Depth comparison bias |
| `shadowNormalBiasVal` | 1.5 | Normal-offset bias |
| `shadowDistance` | 3000.0 | Max shadow range (world units) |
| `cascadeLambda` | 0.92 | Log vs linear cascade split blend (0=linear, 1=log) |

### Other

| Parameter | Default | Description |
|-----------|---------|-------------|
| `ssrMode` | 2 | 0=Sharp, 1=Stochastic, 2=Masked |

---

## 12. Camera System

**File:** `src/client/renderer/Camera.hpp` / `Camera.cpp`

### Configuration

| Parameter | Default | Renderer Override |
|-----------|---------|-------------------|
| `fovy` | 60 degrees | `fovyDegrees = 60.0f` |
| `nearPlane` | 0.1 | `nearPlane = 5.0f` |
| `farPlane` | 100.0 | `farPlane = 15000.0f` |
| `aspect` | 1.0 | Recomputed from window size each frame |

### Matrix Computation (`computeMatrices()`)

```cpp
view = glm::lookAt(eye, target, up);
proj = glm::perspective(glm::radians(fovy), aspect, nearPlane, farPlane);
proj[1][1] *= -1.0f;   // Vulkan Y-flip
```

The Y-flip is necessary because Vulkan NDC has Y pointing downward (opposite of OpenGL).  GLM generates OpenGL-style projection matrices, so the Y axis of the projection is negated to avoid rendering upside-down.  This reverses screen-space winding, which is why the rasterizer state sets `front_face = CLOCKWISE` instead of the usual counter-clockwise.

### Per-Frame Update in drawFrame()

```cpp
void drawFrame(glm::vec3 eye, float yaw, float pitch, float roll)
```

1. Forward direction is computed from yaw/pitch (FPS-style spherical coordinates).
2. If `roll != 0`, the up vector is rotated around the forward axis to produce camera tilt (used during wallrunning and sliding).
3. `camera.setLookAt(eye, eye + forward, camUp)` updates the view matrix.
4. Aspect ratio is recomputed from the swapchain dimensions.

### Utility Methods

| Method | Returns |
|--------|---------|
| `getForward()` | Unit vector from eye toward target |
| `getRight()` | `normalize(cross(forward, up))` |
| `getView()` | View matrix (const ref) |
| `getProjection()` | Projection matrix (const ref) |
| `getViewProjection()` | `proj * view` |

---

## 13. Data Flow Diagram

```
                         drawFrame(eye, yaw, pitch, roll)
                                      |
                    +-----------------+-----------------+
                    |                                   |
            Camera Setup                    Flush Pending Vertex Uploads
            (lookAt, aspect)                (skinned animation data)
                    |                                   |
                    +----------------+------------------+
                                     |
                              Acquire Command Buffer
                              Acquire Swapchain Texture
                              Ensure Render Targets (resize if needed)
                              Upload Particle Data
                              Prepare ImGui Draw Data
                                     |
    =================================================================
    |  PASS 0: Cascaded Shadow Maps (depth-only)                    |
    |                                                               |
    |  Shadow Atlas (D32_FLOAT, 4096x4096)                          |
    |  +-------------------+-------------------+                    |
    |  | Cascade 0 (2048)  | Cascade 1 (2048)  |                    |
    |  |                   |                    |                    |
    |  +-------------------+-------------------+                    |
    |  | Cascade 2 (2048)  | Cascade 3 (2048)  |                    |
    |  |                   |                    |                    |
    |  +-------------------+-------------------+                    |
    |                                                               |
    |  Per cascade:                                                 |
    |    1. Scene models  (shadowPipeline)                          |
    |    2. Entity models  (shadowPipeline)                         |
    |    3. Scene geometry (sceneShadowPipeline)                    |
    =================================================================
                                     |
    =================================================================
    |  PASS 1: Main HDR Colour Pass                                 |
    |  Target: hdrTarget (RGBA16F) + depthTexture (D32_FLOAT)       |
    |                                                               |
    |  Draw order:                                                  |
    |    1. Scene geometry        (scenePipeline)                   |
    |    2. PBR models - opaque   (pbrPipeline)                     |
    |    3. Skybox                (skyboxPipeline)                   |
    |    4. Entity models         (pbrPipeline)                     |
    |    5. PBR models - transparent (pbrTransparentPipeline)       |
    |    6. Particles             (ParticleSystem::render)           |
    |    7. Weapon viewmodel      (pbrPipeline, no shadow)          |
    =================================================================
                                     |
    =================================================================
    |  COMPUTE PASSES (post-processing)                             |
    |                                                               |
    |  hdrTarget ---+---> [GTAO] ---------> ssaoTexture             |
    |               |      [GTAO Blur] ---> ssaoBlurTexture         |
    |               |                                               |
    |               +---> [Bloom Down] ---> bloomMips[0..5]         |
    |               |     [Bloom Up]   ---> bloomMips[0] (final)    |
    |               |                                               |
    |               +---> [SSR] ----------> ssrTexture[dst]         |
    |                      (+ depth, prev SSR, motion vectors)      |
    |                                                               |
    |  depthTexture --+--> [Volumetric] --> volumetricTexture       |
    |                 |    (+ shadowMap)    (half-res)               |
    |                 |                                             |
    |                 +--> [Motion Vec] --> motionVectorTexture      |
    |                      (+ prevVP)                               |
    |                                                               |
    |  hdrTarget + history + motionVec --> [TAA] --> taaHistory[dst] |
    =================================================================
                                     |
    =================================================================
    |  PASS 2: Tone Mapping (HDR -> LDR Swapchain)                  |
    |                                                               |
    |  Inputs: hdrTarget + bloom[0] + ssaoBlur + SSR + volumetric   |
    |  Output: Swapchain texture (LDR)                              |
    |  Shader: fullscreen.vert + tonemap.frag (ACES, gamma 2.2)    |
    |                                                               |
    |  ImGui overlay rendered on top                                |
    =================================================================
                                     |
                            Submit Command Buffer
```

### Shader File Reference

| Shader | Stage | Used By |
|--------|-------|---------|
| `projective.vert` | Vertex | Scene geometry, scene shadow |
| `normal.frag` | Fragment | Scene geometry (PBR lit with shadows) |
| `pbr.vert` | Vertex | PBR models (opaque + transparent), entities, weapon |
| `pbr.frag` | Fragment | PBR models (opaque + transparent), entities, weapon |
| `skybox.vert` | Vertex | Skybox |
| `skybox.frag` | Fragment | Skybox (procedural + cubemap) |
| `shadow.vert` | Vertex | Shadow map (Assimp models) |
| `shadow.frag` | Fragment | Shadow map (no-op, required by SDL3 GPU) |
| `fullscreen.vert` | Vertex | Tonemap fullscreen triangle |
| `tonemap.frag` | Fragment | HDR-to-LDR compositing |
| `gtao.comp` | Compute | Ground Truth Ambient Occlusion |
| `gtao_blur.comp` | Compute | SSAO bilateral blur |
| `bloom_downsample.comp` | Compute | Bloom downsample chain |
| `bloom_upsample.comp` | Compute | Bloom upsample chain |
| `ssr.comp` | Compute | Screen-space reflections |
| `volumetric.comp` | Compute | Volumetric lighting |
| `motion_vectors.comp` | Compute | Screen-space motion vectors |
| `taa.comp` | Compute | Temporal anti-aliasing |
| `brdf_lut.comp` | Compute | BRDF LUT generation (init-time) |
| `irradiance.comp` | Compute | Irradiance map generation (init-time) |
| `prefilter.comp` | Compute | Prefilter map generation (init-time) |

### UBO Binding Layout Summary

**PBR vertex shader (slot 0):**
```
Matrices { model, view, projection, normalMatrix }
```

**PBR fragment shader:**
- Slot 0: `MaterialUBO { baseColorFactor, metallicFactor, roughnessFactor, aoStrength, normalScale, emissiveFactor }`
- Slot 1: `LightDataUBO { cameraPos, ambientColor, numLights, lights[8] }`
- Slot 2: `ShadowDataFragUBO { lightVP[4], cascadeSplits, cameraView, shadowBias, shadowNormalBias, shadowMapSize, lightDirWorld, lightColor, ambientColor, fillColor }`

**PBR fragment samplers (0-7):** albedo, metallic-roughness, emissive, normal, irradiance cubemap, prefilter cubemap, BRDF LUT, shadow map.

**Tonemap fragment shader (slot 0):**
```
TonemapParamsUBO { exposure, gamma, tonemapMode, bloomStrength, ssaoStrength, ssrStrength, volumetricStrength, sharpenStrength }
```

**Tonemap fragment samplers (0-4):** HDR colour, bloom, SSAO blur, SSR, volumetric.
