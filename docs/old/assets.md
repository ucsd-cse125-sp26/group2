# Asset and Animation System Documentation

This document describes the asset loading, material, and skeletal animation
systems used by the engine.  It covers the full data flow from files on disk
through Assimp/stb_image import, CPU-side intermediate representations, GPU
buffer upload, and per-frame rendering.

---

## 1. Asset Organization

### Directory Structure

```
assets/
  Apex_Legend_Wraith.glb          Player character model (Wraith)
  apex_legend_wraith.glb          (duplicate / case variant)
  r-301_-_apex_legends.glb        First-person weapon model (R-301)
  Standard_Run.fbx                Mixamo skeletal animation test
  bottle_a.glb                    Scene prop
  free_1975_porsche_911_930_turbo.glb   Scene prop (vehicle)
  metallic_pallet_factory_store.glb     Scene prop (industrial)
  uploads_files_812442_HdriFree/  HDR environment maps (.hdr)
  uploads_files_812442_FreePresets/  Atmosphere presets (.atm, binary)
```

### Supported File Types

| Extension | Purpose |
|-----------|---------|
| `.glb`    | glTF Binary -- primary 3D model format (meshes + materials + embedded textures) |
| `.fbx`    | Autodesk FBX -- used for Mixamo skeletal animation with skin weights |
| `.hdr`    | Radiance HDR -- equirectangular environment maps for IBL skybox |
| `.atm`    | Atmosphere presets -- third-party binary format (bundled from HDRIFree pack, not parsed by engine code) |

---

## 2. Model Loading Pipeline

All static model loading flows through:

```
src/client/renderer/ModelLoader.hpp   -- data types (ModelVertex, MaterialData, MeshData, TextureData, LoadedModel)
src/client/renderer/ModelLoader.cpp   -- loadModel() implementation
```

### 2.1 Assimp Import

`loadModel()` creates an `Assimp::Importer` and reads the file with these
post-processing flags:

| Flag | Purpose |
|------|---------|
| `aiProcess_Triangulate` | Convert all polygons to triangles |
| `aiProcess_GenSmoothNormals` | Generate smooth vertex normals where missing |
| `aiProcess_CalcTangentSpace` | Compute tangent/bitangent for normal mapping |
| `aiProcess_JoinIdenticalVertices` | Weld duplicate vertices to reduce index count |
| `aiProcess_FlipUVs` (optional) | Flip V = 1 - V for models authored with V=0 at bottom (Blender/OBJ convention) |

The caller controls UV flipping via the `flipUVs` parameter.  glTF models
(V=0 at top) typically do not need flipping; OBJ/FBX models often do.

```cpp
bool loadModel(const std::string& path, LoadedModel& outModel, bool flipUVs = false);
```

### 2.2 Scene Graph Traversal and Transform Baking

After import, `processNode()` recursively walks the Assimp scene graph starting
from the root node.  Each node's local transform is accumulated into a world
matrix:

```
worldTransform = parentTransform * aiToGlm(node->mTransformation)
```

Key behaviors during traversal:

- **Position baking**: Vertex positions are transformed to world space via
  `worldTransform * vec4(localPos, 1.0)`.
- **Normal matrix**: Normals use the inverse-transpose of the upper-left 3x3
  (`glm::transpose(glm::inverse(glm::mat3(worldTransform)))`) so they remain
  correct under non-uniform scale.
- **Negative determinant handling**: If the 3x3 determinant is negative
  (indicating a reflection/mirror), triangle winding is reversed (indices
  1 and 2 swapped) to preserve front-face orientation.
- **Tangent transform**: Tangents are also transformed by the normal matrix.
  Bitangent handedness (w component, +/-1) is recomputed from the triple
  product `dot(cross(N, T), B)`.

### 2.3 Mesh Extraction

Each Assimp mesh produces a `MeshData` containing:

- `std::vector<ModelVertex> vertices` -- see vertex layout below
- `std::vector<uint32_t> indices` -- triangle indices (degenerate faces skipped)
- Per-mesh texture indices and PBR material scalars

#### ModelVertex Layout (48 bytes, no padding)

| Offset | Field | Type | Size |
|--------|-------|------|------|
| 0 | `position` | `vec3` | 12 B |
| 12 | `normal` | `vec3` | 12 B |
| 24 | `texCoord` | `vec2` | 8 B |
| 32 | `tangent` | `vec4` | 16 B |

`tangent.w` stores bitangent handedness (+/-1).  The bitangent is reconstructed
in the shader as `cross(normal, tangent.xyz) * tangent.w`.

### 2.4 Material Extraction

`extractMaterial()` reads PBR metallic-roughness parameters from Assimp
material properties:

| Field | Assimp Key | Default |
|-------|-----------|---------|
| `baseColorFactor` | `AI_MATKEY_BASE_COLOR` (fallback: `AI_MATKEY_COLOR_DIFFUSE`) | `(1, 1, 1, 1)` |
| `metallicFactor` | `AI_MATKEY_METALLIC_FACTOR` | `0.0` (dielectric) |
| `roughnessFactor` | `AI_MATKEY_ROUGHNESS_FACTOR` | `0.5` |
| `emissiveFactor` | `AI_MATKEY_COLOR_EMISSIVE` | `(0, 0, 0, 0)` |
| `alphaMode` | `AI_MATKEY_GLTF_ALPHAMODE` | `Opaque` |
| `alphaCutoff` | `AI_MATKEY_GLTF_ALPHACUTOFF` | `0.5` |
| `aoStrength` | (hardcoded) | `1.0` |
| `normalScale` | (hardcoded) | `1.0` |

Alpha mode detection also checks `AI_MATKEY_OPACITY` -- if opacity < 0.99, the
mesh is treated as `AlphaMode::Blend` even for non-glTF models.

### 2.5 Embedded Texture Decoding

glTF/GLB files embed textures as compressed PNG or JPEG blobs inside the
Assimp scene.  `decodeEmbeddedTexture()` handles two cases:

1. **Compressed** (`mHeight == 0`): The `pcData` pointer holds raw PNG/JPEG
   bytes with `mWidth` as the byte count.  Decoded to RGBA via
   `stbi_load_from_memory()`.
2. **Uncompressed** (`mHeight > 0`): Raw `aiTexel` array copied channel-by-channel
   to RGBA.

Texture resolution uses a deduplication cache (`embTexToDataIdx`) so that
the same embedded texture referenced by multiple materials is decoded only once.

#### Texture Type Resolution

| PBR Slot | Assimp Types Tried (in order) |
|----------|-------------------------------|
| Albedo / Base Color | `aiTextureType_BASE_COLOR`, `aiTextureType_DIFFUSE` |
| Normal Map | `aiTextureType_NORMALS` |
| Metallic-Roughness | `aiTextureType_UNKNOWN` (type 18), `aiTextureType_METALNESS`, `aiTextureType_DIFFUSE_ROUGHNESS`, `aiTextureType_SPECULAR` |
| Ambient Occlusion | `aiTextureType_AMBIENT_OCCLUSION`, `aiTextureType_LIGHTMAP` (fallback) |
| Emissive | `aiTextureType_EMISSIVE` |

Normal and metallic-roughness textures have `isSRGB = false` (linear data).
Albedo and emissive textures keep `isSRGB = true`.

---

## 3. GPU Upload

GPU upload is handled by `Renderer::uploadModel()` and
`Renderer::uploadTexture()`.

### 3.1 Texture Upload with Mip Generation

`Renderer::uploadTexture(pixels, width, height, sRGB)`:

1. Computes the full mip chain: `floor(log2(max(w, h))) + 1` levels.
2. Creates an `SDL_GPUTexture` with format:
   - sRGB textures: `R8G8B8A8_UNORM_SRGB`
   - Linear textures: `R8G8B8A8_UNORM`
3. Uploads mip 0 via a transfer buffer (`SDL_UploadToGPUTexture`).
4. Generates remaining mip levels using GPU blit with `SDL_GPU_FILTER_LINEAR`
   (each level is half the previous, down to 1x1).
5. Tiny 1x1 fallback textures skip mip generation.

### 3.2 ModelInstance Creation

`Renderer::uploadModel(model, outInstance)`:

1. **PBR Sampler** -- Created with linear min/mag/mip filtering, repeat
   addressing, 8x anisotropy, max LOD 13 (supports up to 8192x8192 textures).

2. **Fallback textures** -- Created once per model upload:

   | Fallback | Pixel Value | Purpose |
   |----------|-------------|---------|
   | `fallbackWhite` | `(255, 255, 255, 255)` sRGB | Missing albedo / AO |
   | `fallbackFlatNormal` | `(128, 128, 255, 255)` linear | Missing normal map (flat tangent-space up) |
   | `fallbackMR` | `(0, 128, 0, 255)` linear | Missing metallic-roughness (metallic=0, roughness=0.5) |
   | `fallbackBlack` | `(0, 0, 0, 255)` sRGB | Missing emissive |

3. **Texture upload** -- Each `TextureData` in the model is uploaded via
   `uploadTexture()`.

4. **Geometry upload** -- All meshes are packed into a single transfer buffer:
   - Vertex data (`ModelVertex[]`) followed by index data (`uint32_t[]`) for
     each mesh, sequentially.
   - A single `SDL_GPUCopyPass` uploads all vertex and index buffers.
   - Each mesh gets its own `SDL_GPUBuffer` (one for vertices, one for indices).

5. **GpuMesh records** -- Each mesh stores:
   - Vertex/index buffer handles and index count
   - Texture indices into the `ModelInstance::textures` array
   - `MaterialData` scalars
   - `isTransparent` flag (true when `alphaMode != Opaque`)

### 3.3 Scene Model Loading Entry Points

```cpp
// Load from assets/ directory by filename, apply position/scale transform
int Renderer::loadSceneModel(const char* filename, glm::vec3 pos, float scale, bool flipUVs = false);

// Upload a pre-built LoadedModel (e.g. from SkinnedModel)
int Renderer::uploadSceneModel(const LoadedModel& model);
```

Both return an integer model index into `Renderer::models[]`, or -1 on failure.
Models loaded via `loadSceneModel()` have `drawInScenePass = false` -- they are
only rendered when referenced by an `EntityRenderCmd` or `WeaponViewmodel`.

---

## 4. PBR Material System

### 4.1 MaterialData Struct

```cpp
struct MaterialData {
    glm::vec4 baseColorFactor{1, 1, 1, 1};
    float metallicFactor  = 0.0f;   // Default dielectric (non-metal)
    float roughnessFactor = 0.5f;   // Default mid-roughness
    float aoStrength      = 1.0f;
    float normalScale     = 1.0f;
    glm::vec4 emissiveFactor{0, 0, 0, 0};  // RGB in xyz, w unused
    AlphaMode alphaMode   = AlphaMode::Opaque;
    float alphaCutoff     = 0.5f;   // Threshold for AlphaMode::Mask
};
```

### 4.2 AlphaMode Handling

```cpp
enum class AlphaMode { Opaque, Mask, Blend };
```

| Mode | Pipeline | Behavior |
|------|----------|----------|
| `Opaque` | `pbrPipeline` | Standard depth-tested PBR, no alpha |
| `Mask` | `pbrTransparentPipeline` | Alpha test against `alphaCutoff`; marked transparent |
| `Blend` | `pbrTransparentPipeline` | Alpha-blended rendering |

The `isTransparent` flag on `GpuMesh` is set to `true` for both `Mask` and
`Blend` modes, routing those meshes through the transparent pipeline.

### 4.3 Texture Slots and Fallbacks

During rendering, each mesh binds up to 5 textures.  If a texture index is -1
(not present in the model), the corresponding fallback is used:

| Slot | Texture | Fallback |
|------|---------|----------|
| 0 | Albedo / Base Color | `fallbackWhite` (pure white) |
| 1 | Normal Map | `fallbackFlatNormal` (0.5, 0.5, 1.0) |
| 2 | Metallic-Roughness | `fallbackMR` (metallic=0, roughness=0.5) |
| 3 | Ambient Occlusion | `fallbackWhite` (no occlusion) |
| 4 | Emissive | `fallbackBlack` (no emission) |

---

## 5. Skeletal Animation

### 5.1 Architecture

```
src/client/animation/SkinnedModel.hpp   -- public interface
src/client/animation/SkinnedModel.cpp   -- implementation (PIMPL pattern)
```

The `SkinnedModel` class bridges Assimp scene data and the
[ozz-animation](https://guillaumeblanc.github.io/ozz-animation/) runtime.
All ozz types are hidden behind a PIMPL (`struct Impl`) to keep the header
free of heavy template dependencies.

Key internal types:

| Type | Purpose |
|------|---------|
| `SkinWeight` | Per-vertex: 4 bone indices + 4 weights |
| `MeshSkinData` | Rest-pose vertices, skin weights, and output skinned vertices |
| `JointRestPose` | Cached local translation/rotation/scale for non-animated joints |

### 5.2 load() Flow

`SkinnedModel::load(path)` performs these steps:

1. **Assimp import** with flags:
   - `aiProcess_Triangulate`
   - `aiProcess_GenSmoothNormals`
   - `aiProcess_CalcTangentSpace`
   - `aiProcess_JoinIdenticalVertices`
   - `aiProcess_LimitBoneWeights` (max 4 bones per vertex)
   - `AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS = false` (collapse FBX pivot nodes)

2. **Collect bone names** from all meshes in the scene.

3. **Build ozz skeleton** -- `buildJoint()` recursively walks the Assimp node
   tree, pruning branches that contain no bones.  Each node's local transform
   is decomposed into translation/rotation/scale and stored as a
   `RawSkeleton::Joint`.  Rest poses are cached for step 4.

4. **Build ozz animation** from the first `aiAnimation` in the scene:
   - For each skeleton joint, look up the corresponding `aiNodeAnim` channel.
   - Extract translation, rotation, and scale keyframes with timestamps
     converted from ticks to seconds (`time / ticksPerSecond`).
   - Joints with no animation channel hold at their rest pose (single keyframe
     at t=0).
   - The raw animation is validated and compiled via
     `ozz::animation::offline::AnimationBuilder`.

5. **Allocate runtime buffers**:
   - `locals[]` -- SoA local-space transforms (sized to `num_soa_joints()`)
   - `models[]` -- Per-joint model-space 4x4 matrices
   - `SamplingJob::Context` -- Resized to joint count

6. **Collect inverse bind matrices** from `aiBone::mOffsetMatrix` for each
   bone, mapped to the ozz joint index.  Non-bone structural joints keep
   identity.

7. **Extract mesh vertices and skin weights**:
   - Vertices are stored in mesh-local space (not world-transformed; bone
     transforms handle positioning).
   - Bone weights are distributed to vertices (up to 4 per vertex via
     `aiProcess_LimitBoneWeights`), then normalized so each vertex sums to 1.0.
   - Vertices with zero total weight fall back to joint 0 (root).

8. **First frame**: `update(0.0f)` is called to produce valid geometry, then
   `loadedModel.meshes[i].vertices` is overwritten with the skinned result so
   `getLoadedModel()` returns renderable geometry.

### 5.3 update(dt) -- Per-Frame Animation

`SkinnedModel::update(float dt)` runs 5 stages each frame:

```
1. Advance Clock       playbackTime += dt; wrap via fmod(playbackTime, duration)
        |
2. Sample Animation    ozz::SamplingJob at ratio = playbackTime / duration
        |                -> fills locals[] (SoA local transforms)
3. Local-to-Model      ozz::LocalToModelJob
        |                -> fills models[] (per-joint 4x4 world matrices)
4. Skin Matrices       skinMatrix[j] = models[j] * inverseBindMatrices[j]
        |
5. Linear Blend        For each vertex, for each of 4 bone influences:
   Skinning              pos  += weight * (skinMatrix * vec4(basePos, 1))
                         norm += weight * (mat3(skinMatrix) * baseNormal)
                         tang += weight * (mat3(skinMatrix) * baseTangent)
                       Output is normalized and written to skinnedVertices[]
```

UVs are invariant under skinning and copied directly from the base vertex.

### 5.4 Vertex Re-Upload

After `update()`, the caller retrieves skinned vertices and pushes them to
the GPU:

```cpp
// In Game::iterate() each frame:
runAnimation.update(frameTime);
for (size_t m = 0; m < runAnimation.meshCount(); ++m) {
    const auto& sv = runAnimation.getSkinnedVertices(m);
    renderer.updateModelMeshVertices(
        animatedModelIdx, static_cast<int>(m),
        sv.data(), static_cast<Uint32>(sv.size()));
}
```

`Renderer::updateModelMeshVertices()` does NOT submit a GPU command immediately.
Instead it queues a `PendingVertexUpload` (destination buffer + CPU data copy).
All pending uploads are flushed at the start of the next `drawFrame()` inside
the main command buffer -- zero extra submits, zero pipeline stalls.

The renderer maintains a persistent staging transfer buffer
(`skinTransferBuf`) that is reused across frames with `cycle=true`.

---

## 6. Currently Loaded Assets

Assets loaded during `Game::init()`:

| Asset File | Load Call | Model Index | Purpose |
|------------|-----------|-------------|---------|
| `Apex_Legend_Wraith.glb` | `renderer.loadSceneModel("Apex_Legend_Wraith.glb", vec3(0), 8.0f)` | `wraithModelIdx` | Player entity rendering (Wraith character model, scale 8x) |
| `r-301_-_apex_legends.glb` | `renderer.loadSceneModel("r-301_-_apex_legends.glb", vec3(0), 1.0f)` | `weaponModelIdx` | First-person weapon viewmodel (R-301 carbine) |
| `Standard_Run.fbx` | `runAnimation.load(fbxPath)` then `renderer.uploadSceneModel(...)` | `animatedModelIdx` | Mixamo skeletal animation test (running character, placed at world position (0, 0, 400)) |

All three models are loaded with `drawInScenePass = false` -- they are only
rendered when referenced by:
- `EntityRenderCmd` (player entities via ECS `Renderable` component)
- `WeaponViewmodel` (first-person weapon, rendered after depth clear)

The Wraith model is attached to the local player entity as a `Renderable`
component with scale `vec3(8.0)`.  The animated model entity is spawned at
position `(0, 0, 400)` with scale `vec3(1.0)`.

Additional static models in the `assets/` directory (`bottle_a.glb`,
`free_1975_porsche_911_930_turbo.glb`, `metallic_pallet_factory_store.glb`)
are available but not loaded at startup by default.

---

## 7. HDR Skybox Loading

### Pipeline

```
.hdr file on disk
    |
    v
stbi_loadf()  -- load equirectangular HDR (float RGB per pixel)
    |
    v
CPU cubemap conversion  -- for each of 6 faces (512x512):
    |                       sample equirectangular map via bilinear interpolation
    |                       convert float RGB -> RGBA float16 (half precision)
    v
SDL_UploadToGPUTexture()  -- upload each face to a RGBA16F cubemap layer
    |
    v
IBL regeneration  -- recompute irradiance map + pre-filtered specular map
    |                 from the new cubemap data
    v
envCubemap ready  -- used for skybox rendering + image-based lighting
```

### Runtime Switching

- `Renderer::scanHDRFiles()` scans `assets/uploads_files_812442_HdriFree/`
  for `.hdr` and `.exr` files, populating `availableHDRFiles`.
- The DebugUI presents these as clickable buttons; clicking one calls
  `Renderer::loadHDRSkybox(path)`.
- The old cubemap is released before the new one is created.
- `useHDRSkybox` is set to `true` when an HDR environment is active;
  `currentHDRName` stores the display name.
- When no HDR is loaded, a procedural gradient skybox is used instead.

### Available HDR Environments

```
AmbienceExposure4k.hdr    DayInTheClouds4k.hdr     PlanetaryEarth4k.hdr
CasualDay4K.hdr           FluffballDay4k.hdr       SkyhighFluffycloudField4k.hdr
CloudedSunGlow4k.hdr      HighFantasy4k.hdr        SunlessCirruscover4k.hdr
Cloudymorning4k.hdr       MegaSun4k.hdr            UnderTheSea4k.hdr
CoriolisNight4k.hdr       DarkStorm4K.hdr          UnearthlyRed4k.hdr
```

---

## 8. Atmosphere Presets

The `assets/uploads_files_812442_FreePresets/` directory contains `.atm` files
from a third-party HDRIFree preset pack.  These are **binary files** (not
human-readable text) bundled alongside the HDR environments.

The engine does **not** currently parse or load `.atm` files at runtime.
They are included as reference data from the HDR environment asset pack.

Available presets: CasualDay, CloudedSunGlow, CloudyMorning, CoriolisNight,
DarkStorm, Exposure, FluffballDay, HighFantasy, MegaSun, SkyhighCloudfield,
SunlessCirruscover, UnearthlyRed.

---

## 9. How to Add New Models

### Step-by-step

1. **Place the model file** in `assets/` (`.glb` preferred for static models,
   `.fbx` for skeletal animation).

2. **For a static model** loaded at init time, add to `Game::init()`:
   ```cpp
   int myModelIdx = renderer.loadSceneModel("my_model.glb", glm::vec3(0.0f), 1.0f);
   if (myModelIdx < 0)
       SDL_Log("WARNING: my_model failed to load");
   ```
   - First argument: filename relative to `assets/`.
   - Second argument: world position offset (baked into the instance transform).
   - Third argument: uniform scale factor.
   - Optional fourth argument: `true` to flip UVs (needed for OBJ/Blender
     exports where textures appear upside-down).

3. **To render the model** via the ECS, attach a `Renderable` component:
   ```cpp
   registry.emplace<Renderable>(entity, Renderable{
       .modelIndex = myModelIdx,
       .scale = glm::vec3(1.0f)
   });
   ```

4. **For a pre-built model** (e.g. procedural geometry or a `SkinnedModel`),
   use:
   ```cpp
   int idx = renderer.uploadSceneModel(myLoadedModel);
   ```

### Texture requirements

- Embedded textures (glTF/GLB) are handled automatically.
- External texture files are NOT supported -- textures must be embedded in the
  model file.
- Albedo and emissive textures are treated as sRGB; normal and
  metallic-roughness maps are treated as linear.

### PBR material tips

- The engine uses glTF metallic-roughness workflow.  ORM (occlusion-roughness-
  metallic) packed textures follow the convention: R=AO, G=roughness, B=metallic.
- If your model appears too dark or too shiny, check that `metallicFactor` and
  `roughnessFactor` are set correctly in the model file.
- Missing textures are replaced with sensible fallbacks (see section 4.3).

---

## 10. How to Play an Animation

### Step-by-step

1. **Prepare an FBX file** with skeleton, skin weights, and at least one
   animation clip (Mixamo exports work out of the box).

2. **Add a `SkinnedModel` member** to your game class:
   ```cpp
   #include "animation/SkinnedModel.hpp"
   SkinnedModel myAnimation;
   ```

3. **Load the FBX** during initialization:
   ```cpp
   std::string fbxPath = std::string(SDL_GetBasePath()) + "assets/MyAnimation.fbx";
   if (!myAnimation.load(fbxPath)) {
       SDL_Log("Animation failed to load");
   }
   ```
   `load()` returns false if the file has no skeleton, no bones, or no
   animation clips.

4. **Upload the initial mesh to the GPU**:
   ```cpp
   int myAnimIdx = renderer.uploadSceneModel(myAnimation.getLoadedModel());
   ```

5. **Each frame, advance the animation and re-upload vertices**:
   ```cpp
   myAnimation.update(dt);  // dt in seconds
   for (size_t m = 0; m < myAnimation.meshCount(); ++m) {
       const auto& verts = myAnimation.getSkinnedVertices(m);
       renderer.updateModelMeshVertices(
           myAnimIdx, static_cast<int>(m),
           verts.data(), static_cast<Uint32>(verts.size()));
   }
   ```
   The animation loops automatically via `fmod(playbackTime, duration)`.

6. **Spawn an entity** with the animated model:
   ```cpp
   auto entity = registry.create();
   registry.emplace<Position>(entity, glm::vec3(0, 0, 0));
   registry.emplace<PreviousPosition>(entity, glm::vec3(0, 0, 0));
   registry.emplace<Renderable>(entity, Renderable{
       .modelIndex = myAnimIdx,
       .scale = glm::vec3(1.0f)
   });
   ```

### Key API

| Method | Description |
|--------|-------------|
| `load(path)` | Parse FBX, build skeleton + animation, extract skin weights. Returns true on success. |
| `update(dt)` | Advance clock, sample skeleton, perform linear-blend skinning on CPU. |
| `getLoadedModel()` | Returns `LoadedModel&` for initial GPU upload (valid after `load()`). |
| `getSkinnedVertices(meshIdx)` | Returns current frame's skinned vertex data for one mesh. |
| `meshCount()` | Number of skinned meshes. |
| `duration()` | Animation clip length in seconds. |
| `isLoaded()` | True after successful `load()`. |

### Limitations

- Only the **first animation clip** in the FBX file is used.
- Skinning is performed on the **CPU** (linear blend, 4 bones per vertex).
- Vertex data is re-uploaded every frame via deferred GPU copy.
- Mixamo FBX files typically do not embed textures; the model renders with a
  default grey matte material (baseColor=0.7, metallic=0, roughness=0.8).

---

## Appendix: Complete Data Flow

```
                        DISK
                         |
            .glb / .fbx / .hdr file
                         |
                    +-----------+
                    |  Assimp   |    (or stbi_loadf for HDR)
                    |  Importer |
                    +-----------+
                         |
        +----------------+----------------+
        |                |                |
   aiScene          aiMaterial        aiTexture
   (nodes,          (PBR scalars,     (embedded
    meshes)          alpha mode)       PNG/JPEG)
        |                |                |
        v                v                v
   processNode()    extractMaterial()  decodeEmbeddedTexture()
   (scene graph     (MaterialData)    (stb_image -> RGBA pixels)
    traversal,                              |
    transform                               v
    baking)                           TextureData
        |                                   |
        v                                   |
   MeshData                                 |
   (ModelVertex[],                          |
    uint32_t[],                             |
    tex indices,                            |
    MaterialData)                           |
        |                                   |
        +-----------------------------------+
        |
        v
   LoadedModel  { meshes[], textures[] }
        |
        +---> Renderer::uploadModel()
        |         |
        |    +----+----+
        |    |         |
        |    v         v
        | uploadTexture()   GPU Buffer Upload
        | (mip gen via      (transfer buffer ->
        |  GPU blit)         vertex + index buffers)
        |    |         |
        |    v         v
        | SDL_GPUTexture[]   GpuMesh[]
        |         |              |
        |         v              v
        |    ModelInstance { meshes[], textures[], transform }
        |         |
        |         v
        |    models[] vector (indexed by model ID)
        |         |
        |         v
        |    drawFrame()
        |      - Bind vertex/index buffers
        |      - Bind textures (or fallbacks)
        |      - Push MaterialData as uniform
        |      - Draw indexed (opaque pipeline or transparent pipeline)
        |
        +---> SkinnedModel (for animated models)
                  |
             load() -> ozz skeleton + animation
                  |
             update(dt) each frame
                  |  1. SamplingJob (sample animation)
                  |  2. LocalToModelJob (joint matrices)
                  |  3. Linear blend skinning (CPU)
                  |
             getSkinnedVertices()
                  |
                  v
             Renderer::updateModelMeshVertices()
                  |
             PendingVertexUpload queue
                  |
                  v
             drawFrame() flushes uploads
             (transfer buffer -> vertex buffer copy)
```
