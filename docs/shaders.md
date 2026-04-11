# Shader Reference

## Overview

All shaders are written in **GLSL 450** and compiled to **SPIR-V** for use with the SDL3 GPU shader interface. The engine targets **Vulkan** natively and supports **Metal** via `spirv-cross` cross-compilation, providing full cross-platform coverage on Linux, Windows, and macOS.

Shaders live in `/shaders/` as `.vert`, `.frag`, and `.comp` source files. They are compiled offline to `.spv` binaries and loaded at runtime through SDL3's GPU pipeline creation API.

### SDL3 GPU Descriptor Set Convention

SDL3 GPU remaps GLSL descriptor sets differently for each shader stage:

| Stage    | set 0                          | set 1           | set 2                          | set 3           |
|----------|--------------------------------|-----------------|--------------------------------|-----------------|
| Vertex   | Storage buffers / textures     | Uniform buffers | --                             | --              |
| Fragment | --                             | --              | Sampled textures / storage     | Uniform buffers |
| Compute  | Sampled textures (read)        | Storage images (write) | Uniform buffers         | --              |

This convention is consistent across all shaders in the codebase.

### Workgroup Sizes

All compute shaders use `local_size_x = 16, local_size_y = 16, local_size_z = 1` unless otherwise noted. Cubemap compute shaders (irradiance, prefilter) dispatch with z = 6 for the six cube faces.

---

## Shader Categories

---

### 1. Geometry / Rendering Shaders

#### `pbr.vert` -- PBR Vertex Shader

Full vertex shader for Assimp-loaded meshes with tangent-space normal mapping support.

**Vertex Inputs:**

| Location | Type   | Name         | Description                                      |
|----------|--------|--------------|--------------------------------------------------|
| 0        | `vec3` | `inPosition` | Vertex position (object space)                   |
| 1        | `vec3` | `inNormal`   | Vertex normal (object space)                     |
| 2        | `vec2` | `inTexCoord` | Texture coordinate                               |
| 3        | `vec4` | `inTangent`  | xyz = tangent, w = bitangent sign (plus/minus 1) |

Vertex layout matches `ModelVertex` at 48 bytes stride.

**Outputs to Fragment:**

| Location | Type   | Name           | Description                |
|----------|--------|----------------|----------------------------|
| 0        | `vec3` | `fragWorldPos` | World-space position       |
| 1        | `vec3` | `fragNormal`   | World-space normal         |
| 2        | `vec2` | `fragTexCoord` | Pass-through UV            |
| 3        | `vec3` | `fragTangent`  | World-space tangent        |
| 4        | `vec3` | `fragBitangent`| World-space bitangent      |

**Uniform Buffer (set 1, binding 0) -- `Matrices`:**

```glsl
mat4 model;
mat4 view;
mat4 projection;
mat4 normalMatrix;   // transpose(inverse(model)), padded to mat4 for std140
```

The normal matrix is precomputed on the CPU and passed as a full `mat4` (std140 padding). Tangent and bitangent are transformed via the upper-left 3x3 of `normalMatrix`. The bitangent is reconstructed as `cross(N, T) * inTangent.w` to handle mirrored UVs.

---

#### `pbr.frag` -- PBR Fragment Shader (Cook-Torrance GGX)

Full physically-based rendering fragment shader implementing the **Cook-Torrance microfacet BRDF** with the **metallic-roughness** workflow. Outputs linear HDR color (no tone mapping).

**Inputs:** Matches `pbr.vert` outputs (locations 0--4).

**Output:**

| Location | Type   | Name       | Description                                  |
|----------|--------|------------|----------------------------------------------|
| 0        | `vec4` | `outColor` | Linear HDR color, alpha from material/texture |

**Texture Samplers (set 2):**

| Binding | Type                | Name                   | Description                               |
|---------|---------------------|------------------------|-------------------------------------------|
| 0       | `sampler2D`         | `texAlbedo`            | Base color / albedo map                   |
| 1       | `sampler2D`         | `texMetallicRoughness` | B = metallic, G = roughness (glTF convention) |
| 2       | `sampler2D`         | `texEmissive`          | Emissive color map                        |
| 3       | `sampler2D`         | `texNormal`            | Tangent-space normal map                  |
| 4       | `samplerCube`       | `irradianceMap`        | Diffuse IBL irradiance cubemap            |
| 5       | `samplerCube`       | `prefilterMap`         | Specular IBL pre-filtered environment     |
| 6       | `sampler2D`         | `brdfLUT`              | Split-sum BRDF integration LUT            |
| 7       | `sampler2DShadow`   | `shadowMap`            | Cascaded shadow map atlas (2x2 grid)      |

**Uniform Buffers (set 3):**

**Binding 0 -- `Material`:**

```glsl
vec4  baseColorFactor;
float metallicFactor;
float roughnessFactor;
float aoStrength;
float normalScale;
vec4  emissiveFactor;   // rgb in xyz, w unused
```

**Binding 1 -- `LightData`:**

```glsl
vec4  cameraPos;       // xyz = world-space eye position
vec4  ambientColor;    // rgb = ambient radiance
int   numLights;
float _pad1, _pad2, _pad3;
Light lights[8];       // array of up to 8 lights
```

Where `Light` is:

```glsl
struct Light {
    vec4 position;   // xyz = direction (w=0 directional) or position (w=1 point)
    vec4 color;      // rgb = color, a = intensity
    vec4 params;     // x = range, y = innerCone, z = outerCone, w = castsShadow
};
```

**Binding 2 -- `ShadowData`:**

```glsl
mat4  lightVP[4];         // Per-cascade light view-projection matrices
vec4  cascadeSplits;      // View-space far distances for each cascade
mat4  cameraView;         // Camera view matrix
float shadowBias;
float shadowNormalBias;
float shadowMapSize;      // Per-cascade resolution (e.g., 2048)
float _shadowPad;
```

**BRDF Details:**

- **Normal Distribution:** GGX/Trowbridge-Reitz (`distributionGGX`)
- **Geometry:** Smith-GGX with Schlick-GGX approximation, direct-lighting k remapping
- **Fresnel:** Schlick approximation, with roughness-aware variant for IBL
- **Diffuse:** Lambertian, energy-conserving (scaled by `(1 - F) * (1 - metallic)`)
- **IBL:** Split-sum approximation with irradiance cubemap (diffuse) + pre-filtered environment (specular) + BRDF LUT
- **Specular Occlusion:** UE-style suppression at grazing angles to prevent bright edge artifacts
- **Shadows:** 4-cascade CSM with 3x3 PCF, normal-offset bias, inter-cascade blending in last 10% of each range
- **Shadow Atlas:** 2x2 grid layout. Cascade 0 at (0,0), cascade 1 at (0.5,0), cascade 2 at (0,0.5), cascade 3 at (0.5,0.5)

---

#### `model.vert` -- Simple Model Vertex Shader

Simplified vertex shader for Assimp-loaded meshes without tangent/bitangent support. Computes the normal matrix inline via `transpose(inverse(model))`.

**Vertex Inputs:**

| Location | Type   | Name         | Description            |
|----------|--------|--------------|------------------------|
| 0        | `vec3` | `inPosition` | Vertex position        |
| 1        | `vec3` | `inNormal`   | Vertex normal          |
| 2        | `vec2` | `inTexCoord` | Texture coordinate     |

**Outputs:**

| Location | Type   | Name           | Description          |
|----------|--------|----------------|----------------------|
| 0        | `vec3` | `fragNormal`   | World-space normal   |
| 1        | `vec3` | `fragWorldPos` | World-space position |
| 2        | `vec2` | `fragTexCoord` | Pass-through UV      |

**Uniform Buffer (set 1, binding 0) -- `Matrices`:**

```glsl
mat4 model;
mat4 view;
mat4 projection;
```

Note: This is the simpler 3-matrix variant (no precomputed `normalMatrix`). Distinct from `pbr.vert` in that it lacks tangent inputs and the fourth matrix.

---

#### `model.frag` -- Simple Diffuse Fragment Shader

Basic two-light diffuse lighting with ambient. No PBR, no shadows, no IBL. Intended as a lightweight fallback or debug shader.

**Inputs:** Matches `model.vert` outputs (locations 0--2).

**Output:** `outColor` at location 0.

**Texture Samplers (set 2):**

| Binding | Type        | Name         | Description                              |
|---------|-------------|--------------|------------------------------------------|
| 0       | `sampler2D` | `texDiffuse` | Base-color texture (1x1 white fallback)  |

**Lighting:** Hard-coded two directional lights:
- Primary: direction `(0.5, 1.0, 0.5)`, intensity 0.55
- Fill: direction `(-0.5, 0.3, -0.8)`, intensity 0.35
- Ambient: 0.35

---

#### `projective.vert` -- Procedural Cube Vertex Shader

Generates a unit cube entirely from `gl_VertexIndex` -- no vertex buffer needed. Hard-coded 24 vertex positions, 24 normals, 36 indices, and 6 per-face colors.

**Vertex Inputs:** None (procedural from `gl_VertexIndex`).

**Outputs:**

| Location | Type        | Name         | Description              |
|----------|-------------|--------------|--------------------------|
| 0        | `vec3`      | `diffuse`    | Per-face color           |
| 1        | `flat vec3` | `fragNormal` | Face normal (flat shaded)|

**Uniform Buffer (set 1, binding 0) -- `Matrices`:** Same `model/view/projection` layout.

Draw call: 36 vertices, no vertex buffer.

---

#### `geometry.vert` -- Procedural Scene Geometry

Generates an entire physics test playground procedurally from `gl_VertexIndex`. All geometry is defined as compile-time constants: boxes, ramps, a diagonal wall, and a floor quad.

**Vertex Inputs:** None (procedural from `gl_VertexIndex`).

**Geometry Contents (1182 total vertices):**

| Range       | Count | Description                                |
|-------------|-------|--------------------------------------------|
| 0--1079     | 1080  | 30 axis-aligned boxes (36 verts each)      |
| 1080--1139  | 60    | 2 wedge ramps (30 verts each)              |
| 1140--1175  | 36    | 1 diagonal wall (45-degree, 36 verts)      |
| 1176--1181  | 6     | 1 floor quad (16000x16000 units)           |

Notable objects: reference cube, step/jump boxes, staircase (5 steps), axis-aligned wall, pole, elevated walkway, wallrun corridors, climb/ledge walls, slide guide walls, parkour course, and grapple test architecture (arch with pillars, crossbar, elevated platforms).

**Outputs:**

| Location | Type        | Name           | Description               |
|----------|-------------|----------------|---------------------------|
| 0        | `vec3`      | `fragColor`    | Per-object/face color     |
| 1        | `vec3`      | `fragWorldPos` | World-space position      |
| 2        | `float`     | `fragIsFloor`  | 1.0 for floor, 0.0 else  |
| 3        | `flat vec3` | `fragNormal`   | Face normal (flat shaded) |

**Uniform Buffer (set 1, binding 0) -- `Matrices`:** Same `model/view/projection` layout.

---

#### `geometry.frag` -- Scene Geometry Fragment Shader

This file is identical to `normal.frag` (see below). Both filenames refer to the same lit scene fragment shader with shadows.

---

#### `normal.frag` -- Lit Scene Fragment Shader (Two Variants)

There are two versions of `normal.frag` paired with different vertex shaders:

**Variant A (paired with `projective.vert`):**

Simple flat-shaded Lambertian lighting with a single hard-coded directional light `(1,1,1)` and 6.25% ambient. No shadows, no textures.

| Input Location | Type        | Name         |
|----------------|-------------|--------------|
| 0              | `vec3`      | `diffuse`    |
| 1              | `flat vec3` | `fragNormal` |

**Variant B (paired with `geometry.vert`):**

Full lit scene with cascaded shadow maps, hemisphere ambient, sun + fill lights, and a procedural grid floor. All lighting parameters come from UBO (driven by ImGui sliders).

| Input Location | Type        | Name           |
|----------------|-------------|----------------|
| 0              | `vec3`      | `fragColor`    |
| 1              | `vec3`      | `fragWorldPos` |
| 2              | `float`     | `fragIsFloor`  |
| 3              | `flat vec3` | `fragNormal`   |

**Texture Samplers (set 2):**

| Binding | Type              | Name        | Description              |
|---------|-------------------|-------------|--------------------------|
| 0       | `sampler2DShadow` | `shadowMap` | Cascaded shadow map atlas |

**Uniform Buffer (set 3, binding 0) -- `SceneShadowData`:**

```glsl
mat4  lightVP[4];        // Per-cascade light view-projection matrices
vec4  cascadeSplits;     // View-space far distances
mat4  cameraView;        // Camera view matrix
float shadowBias;
float shadowNormalBias;
float shadowMapSize;     // Per-cascade resolution
float _pad;
vec4  lightDirWorld;     // xyz = direction TO sun
vec4  lightColor;        // rgb = color, a = intensity
vec4  ambientColor;      // rgb = ambient color
vec4  fillColor;         // rgb = fill color, a = fill intensity
```

Features: 4-cascade CSM with 3x3 PCF, inter-cascade blending, hemisphere ambient (sky/ground), sun + fill light, procedural red matte floor with black grid lines (100-unit spacing).

---

#### `shadow.vert` -- Shadow Map Vertex Shader

Depth-only vertex shader for the shadow map pass. Minimal: transforms position by `lightVP * model`.

**Vertex Inputs:**

| Location | Type   | Name         | Description    |
|----------|--------|--------------|----------------|
| 0        | `vec3` | `inPosition` | Vertex position|

**Uniform Buffer (set 1, binding 0) -- `LightMatrices`:**

```glsl
mat4 lightVP;
mat4 model;
```

**Outputs:** Only `gl_Position` (depth is written automatically).

---

#### `shadow.frag` -- Shadow Map Fragment Shader

Minimal no-op fragment shader. SDL3 GPU requires a fragment shader even for depth-only passes. No color output, no inputs.

---

#### `skybox.vert` -- Skybox Vertex Shader

Generates a unit cube from `gl_VertexIndex` (36 vertices, no vertex buffer). Sets depth to `w` (far plane) so the skybox always renders behind all geometry.

**Vertex Inputs:** None (procedural).

**Outputs:**

| Location | Type   | Name      | Description                        |
|----------|--------|-----------|------------------------------------|
| 0        | `vec3` | `fragDir` | Cube sampling direction (= position) |

**Uniform Buffer (set 1, binding 0) -- `SkyboxMatrices`:**

```glsl
mat4 viewRotation;   // View matrix with translation zeroed (rotation only)
mat4 projection;
```

Key technique: `gl_Position = clipPos.xyww` forces depth = 1.0 (far plane).

---

#### `skybox.frag` -- Skybox Fragment Shader

Dual-mode skybox: samples an HDR cubemap when available, falls back to a procedural gradient sky.

**Inputs:**

| Location | Type   | Name      | Description           |
|----------|--------|-----------|-----------------------|
| 0        | `vec3` | `fragDir` | Sampling direction    |

**Output:** `outColor` at location 0 (linear HDR).

**Texture Samplers (set 2):**

| Binding | Type          | Name     | Description                          |
|---------|---------------|----------|--------------------------------------|
| 0       | `samplerCube` | `envMap` | HDR environment cubemap              |

**Uniform Buffer (set 3, binding 0) -- `SkyboxParams`:**

```glsl
int   useCubemap;      // 0 = procedural sky, 1 = sample envMap
float envExposure;     // Exposure multiplier for HDR cubemap
float _pad1, _pad2;
vec4  sunDir;          // xyz = direction TO sun
```

**Procedural Sky Features:**
- Three-color gradient: zenith (deep blue), horizon (warm orange), nadir (dark)
- Sun disc with `smoothstep(0.9975, 0.999)` angular threshold
- Sun glow halo: `pow(max(sunAngle, 0), 256)`
- Subtle horizon glow band

---

### 2. Post-Processing Shaders

#### `fullscreen.vert` -- Fullscreen Triangle

Generates a fullscreen triangle from `gl_VertexIndex` (3 vertices, no vertex buffer). Used by all raster-based post-processing passes.

**Vertex Inputs:** None (procedural).

**Output:**

| Location | Type   | Name           | Description        |
|----------|--------|----------------|--------------------|
| 0        | `vec2` | `fragTexCoord` | Screen-space UV    |

Technique: Uses bit manipulation to produce the oversized triangle that covers the full `[-1,1]` NDC quad:
```
vertex 0: (-1,-1)  uv (0,0)
vertex 1: ( 3,-1)  uv (2,0)
vertex 2: (-1, 3)  uv (0,2)
```

---

#### `tonemap.frag` -- HDR Tone Mapping + Composite

Final compositing pass that combines all post-processing buffers and performs HDR-to-LDR tone mapping with gamma correction.

**Inputs:** `fragTexCoord` from `fullscreen.vert`.

**Texture Samplers (set 2):**

| Binding | Type        | Name              | Description             |
|---------|-------------|-------------------|-------------------------|
| 0       | `sampler2D` | `hdrBuffer`       | Main HDR color buffer   |
| 1       | `sampler2D` | `bloomBuffer`     | Bloom mip chain result  |
| 2       | `sampler2D` | `ssaoBuffer`      | SSAO/GTAO result        |
| 3       | `sampler2D` | `ssrBuffer`       | SSR result (RGBA)       |
| 4       | `sampler2D` | `volumetricBuffer`| Volumetric lighting     |

**Uniform Buffer (set 3, binding 0) -- `TonemapParams`:**

```glsl
float exposure;
float gamma;
int   tonemapMode;        // 0 = ACES, 1 = Reinhard, 2 = Linear clamp
float bloomStrength;
float ssaoStrength;
float ssrStrength;
float volumetricStrength;
float sharpenStrength;
```

**Processing Pipeline (in order):**

1. **Sharpening** -- Unsharp mask (4-tap cross filter) on the HDR buffer. Counteracts TAA blur. Applied when `sharpenStrength > 0`.
2. **Bloom** -- Additive blend: `hdr += bloom * bloomStrength`
3. **SSR** -- Alpha-weighted blend: `hdr = mix(hdr, ssr.rgb, ssr.a * ssrStrength)`
4. **Volumetrics** -- Additive blend: `hdr += vol.rgb * volumetricStrength`
5. **SSAO** -- Multiplicative: `hdr *= mix(1.0, ao, ssaoStrength)`
6. **Exposure** -- `hdr *= exposure`
7. **Tone mapping** -- ACES Filmic (Narkowicz 2015), Reinhard, or linear clamp
8. **Gamma correction** -- `pow(ldr, 1.0 / gamma)`

---

#### `bloom_downsample.frag` -- Bloom Downsample (Raster)

13-tap downsample filter for the bloom mip chain, based on the Jimenez 2014 presentation (Call of Duty: Advanced Warfare). Paired with `fullscreen.vert`.

**Texture Samplers (set 2):**

| Binding | Type        | Name         | Description           |
|---------|-------------|--------------|-----------------------|
| 0       | `sampler2D` | `srcTexture` | Source mip level      |

**Uniform Buffer (set 3, binding 0) -- `DownsampleParams`:**

```glsl
vec2  srcResolution;
float isFirstPass;   // 1.0 for Karis average, 0.0 otherwise
float _pad;
```

On the first pass, applies **Karis average** (luma-weighted) to suppress firefly artifacts from bright HDR pixels. Subsequent passes use the standard 13-tap weighted average.

---

#### `bloom_downsample.comp` -- Bloom Downsample (Compute)

Compute shader variant of the bloom downsample. Same 13-tap Karis-weighted filter.

**Bindings:**

| Set | Binding | Type                  | Name         | Description         |
|-----|---------|-----------------------|--------------|---------------------|
| 0   | 0       | `sampler2D`           | `srcTexture` | Source mip          |
| 1   | 0       | `image2D (rgba16f)`   | `dstImage`   | Destination mip     |
| 2   | 0       | `uniform Params`      | --           | `srcResolution`, `isFirstPass` |

Additional feature over the raster variant: the first pass applies a **luminance threshold** -- only pixels with luminance > 1.0 contribute to bloom.

---

#### `bloom_upsample.comp` -- Bloom Upsample (Compute)

9-tap tent filter for smooth upsampling with additive blend into the destination mip level.

**Bindings:**

| Set | Binding | Type                  | Name         | Description                  |
|-----|---------|-----------------------|--------------|------------------------------|
| 0   | 0       | `sampler2D`           | `srcTexture` | Smaller mip (source)         |
| 1   | 0       | `image2D (rgba16f)`   | `dstImage`   | Larger mip (read+write)      |
| 2   | 0       | `uniform Params`      | --           | `srcResolution`, `bloomIntensity` |

The `dstImage` is both read and written: existing content is loaded, the upsampled bloom is added, and the result is stored back. This enables progressive accumulation up the mip chain.

---

#### `ssao.comp` -- Screen-Space Ambient Occlusion

Hemisphere-sampling SSAO. Reconstructs view-space position from depth, samples 32 random points in the hemisphere, and checks occlusion.

**Bindings:**

| Set | Binding | Type                | Name           | Description              |
|-----|---------|---------------------|----------------|--------------------------|
| 0   | 0       | `sampler2D`         | `depthTexture` | Scene depth buffer       |
| 1   | 0       | `image2D (r8)`      | `ssaoImage`    | Output AO (single channel) |
| 2   | 0       | `uniform SSAOParams`| --             | Projection, kernel, etc. |

**`SSAOParams` Layout:**

```glsl
mat4 projection;
mat4 invProjection;
vec4 kernel[32];       // Hemisphere sample offsets (precomputed on CPU)
vec2 noiseScale;       // screen_size / 4.0
float radius;          // AO sample radius
float bias;            // Depth bias to prevent self-occlusion
```

Uses a screen-space hash function for random rotation per pixel (no noise texture needed). Normals are reconstructed from depth cross-derivatives. Sky pixels (depth >= 0.9999) output 1.0 (no occlusion).

---

#### `ssao_blur.comp` -- SSAO Blur

Simple 5x5 box blur for the SSAO buffer.

**Bindings:**

| Set | Binding | Type            | Name         | Description       |
|-----|---------|-----------------|--------------|-------------------|
| 0   | 0       | `sampler2D`     | `ssaoInput`  | Raw SSAO          |
| 1   | 0       | `image2D (r8)`  | `ssaoOutput` | Blurred SSAO      |

No uniform parameters. 25-sample average (5x5 kernel).

---

#### `gtao.comp` -- Ground Truth Ambient Occlusion

XeGTAO-inspired horizon-based AO. Traces screen-space horizon lines in multiple directional slices, computes visible solid angle, and integrates cosine-weighted visibility analytically. Much more stable than random-sample SSAO.

**Bindings:**

| Set | Binding | Type              | Name           | Description        |
|-----|---------|-------------------|----------------|--------------------|
| 0   | 0       | `sampler2D`       | `depthTexture` | Scene depth buffer |
| 1   | 0       | `image2D (r8)`    | `aoImage`      | Output AO          |
| 2   | 0       | `uniform GTAOParams` | --          | Configuration      |

**`GTAOParams` Layout:**

```glsl
mat4 projection;
mat4 invProjection;
vec2 screenSize;
float radius;          // AO radius in view-space units
float falloffExp;      // Distance falloff exponent
int   numSlices;       // Directional slices (typically 3-4)
int   numSteps;        // March steps per direction (typically 4-8)
float _pad1, _pad2;
```

Key techniques:
- **Interleaved gradient noise** for per-pixel slice rotation (reduces banding)
- **Edge-aware normal reconstruction** from depth (picks smaller delta to avoid edge artifacts)
- AO radius projected to screen pixels with clamping (3--256 px)
- Analytical cosine-weighted integration via `gtaoIntegral(h1, h2, n)`

---

#### `gtao_blur.comp` -- GTAO Cross-Bilateral Blur

Edge-preserving spatial filter for GTAO output. Uses depth similarity weighting to prevent AO from bleeding across geometry edges.

**Bindings:**

| Set | Binding | Type              | Name           | Description           |
|-----|---------|-------------------|----------------|-----------------------|
| 0   | 0       | `sampler2D`       | `aoInput`      | Raw GTAO              |
| 0   | 1       | `sampler2D`       | `depthTexture` | Scene depth buffer    |
| 1   | 0       | `image2D (r8)`    | `aoOutput`     | Blurred AO            |
| 2   | 0       | `uniform BlurParams` | --          | `screenSize`, `depthSigma` |

7x7 bilateral kernel with:
- **Spatial weights:** Precomputed Gaussian (sigma ~1.5)
- **Depth weights:** `exp(-depthDiff^2 * depthSigma)` using linearized depth

---

#### `ssr.comp` -- Screen-Space Reflections

Screen-space ray marching with three selectable modes.

**Bindings:**

| Set | Binding | Type                  | Name            | Description              |
|-----|---------|-----------------------|-----------------|--------------------------|
| 0   | 0       | `sampler2D`           | `hdrBuffer`     | Scene HDR color          |
| 0   | 1       | `sampler2D`           | `depthBuffer`   | Scene depth              |
| 0   | 2       | `sampler2D`           | `prevSSR`       | Previous frame SSR (ping-pong) |
| 0   | 3       | `sampler2D`           | `motionVectors` | Per-pixel motion vectors |
| 1   | 0       | `image2D (rgba16f)`   | `ssrImage`      | Output SSR (RGB=color, A=confidence) |
| 2   | 0       | `uniform SSRParams`   | --              | Configuration            |

**`SSRParams` Layout:**

```glsl
mat4  projection;
mat4  invProjection;
mat4  view;
vec2  screenSize;
float maxDistance;
float thickness;
float frameIndex;
float jitterStrength;
int   ssrMode;          // 0=Sharp, 1=Stochastic, 2=Masked
float _pad1, _pad2, _pad3;
```

**Modes:**

| Mode | Name       | Description                                                              |
|------|------------|--------------------------------------------------------------------------|
| 0    | Sharp      | Deterministic ray march with proximity fade (screen-space travel distance) |
| 1    | Stochastic | Jittered rays + temporal accumulation via ping-pong history buffer       |
| 2    | Masked     | Deterministic ray march with world-space distance fade at contact zone   |

Common features:
- **Normal reconstruction** from depth central differences (picks smaller delta at edges)
- **Surface filtering:** Only near-vertical surfaces (walls) get SSR; floors are suppressed
- **Binary refinement:** 5 iterations after initial hit detection
- **Edge fade:** Smooth fade near screen edges to prevent hard cutoffs
- **Distance fade:** Confidence decreases with march distance

---

#### `sss.comp` -- Subsurface Scattering

Screen-space separable Gaussian blur for subsurface scattering on skin/organic materials. Dispatched twice (horizontal + vertical) for the full separable filter.

**Bindings:**

| Set | Binding | Type                  | Name         | Description         |
|-----|---------|-----------------------|--------------|---------------------|
| 0   | 0       | `sampler2D`           | `hdrInput`   | HDR color buffer    |
| 0   | 1       | `sampler2D`           | `depthBuffer`| Scene depth         |
| 1   | 0       | `image2D (rgba16f)`   | `sssOutput`  | Blurred output      |
| 2   | 0       | `uniform SSSParams`   | --           | Configuration       |

**`SSSParams` Layout:**

```glsl
vec2  screenSize;
vec2  blurDir;        // (1,0) for horizontal, (0,1) for vertical
float sssWidth;       // World-space scattering width
float _pad1, _pad2, _pad3;
```

7-tap depth-aware Gaussian with weights `[0.383, 0.242, 0.061, 0.006]`. Blur radius scales inversely with depth (closer objects get wider scattering).

---

#### `taa.comp` -- Temporal Anti-Aliasing

Temporal accumulation with neighborhood clamping to reduce ghosting.

**Bindings:**

| Set | Binding | Type                  | Name            | Description            |
|-----|---------|-----------------------|-----------------|------------------------|
| 0   | 0       | `sampler2D`           | `currentFrame`  | Current frame HDR      |
| 0   | 1       | `sampler2D`           | `historyFrame`  | Previous frame result  |
| 0   | 2       | `sampler2D`           | `motionVectors` | Per-pixel motion       |
| 1   | 0       | `image2D (rgba16f)`   | `outputImage`   | TAA output             |
| 2   | 0       | `uniform TAAParams`   | --              | Configuration          |

**`TAAParams` Layout:**

```glsl
vec2  screenSize;
float blendFactor;     // 0.1 = 10% current, 90% history
float _pad;
```

Features:
- **3x3 neighborhood clamping** (min/max of current frame neighbors) to reduce ghosting
- **History rejection** when reprojected UV is off-screen
- Motion-vector-based reprojection from previous frame

---

#### `motion_vectors.comp` -- Per-Pixel Motion Vectors

Computes screen-space motion vectors for TAA and temporal effects by reprojecting the current frame's depth to the previous frame's screen space.

**Bindings:**

| Set | Binding | Type                   | Name          | Description            |
|-----|---------|------------------------|---------------|------------------------|
| 0   | 0       | `sampler2D`            | `depthBuffer` | Current frame depth    |
| 1   | 0       | `image2D (rg16f)`      | `motionImage` | Output motion vectors  |
| 2   | 0       | `uniform MotionParams` | --            | VP matrices, jitter    |

**`MotionParams` Layout:**

```glsl
mat4 currentInvVP;    // Inverse of current frame's ViewProjection
mat4 previousVP;      // Previous frame's ViewProjection
vec2 screenSize;
vec2 jitterOffset;    // Current frame's sub-pixel jitter
```

Outputs RG16F: `motion = currentUV - previousUV`. Sky pixels (depth >= 0.9999) output zero motion.

---

#### `volumetric.comp` -- Volumetric Lighting / God Rays

Ray-marched volumetric light scattering using cascaded shadow maps. Renders at half resolution for performance.

**Bindings:**

| Set | Binding | Type                      | Name              | Description            |
|-----|---------|---------------------------|-------------------|------------------------|
| 0   | 0       | `sampler2D`               | `depthBuffer`     | Scene depth            |
| 0   | 1       | `sampler2DShadow`         | `shadowMap`       | CSM shadow atlas       |
| 1   | 0       | `image2D (rgba16f)`       | `volumetricImage` | Output scattering      |
| 2   | 0       | `uniform VolumetricParams`| --                | Configuration          |

**`VolumetricParams` Layout:**

```glsl
mat4 invViewProj;
mat4 lightVP[4];          // Per-cascade light VP matrices
mat4 cameraView;          // Camera view matrix
vec4 cascadeSplits;       // Cascade far distances
vec4 lightDir;            // xyz = direction to light
vec4 lightColor;          // rgb = color, a = intensity
vec2 screenSize;          // Full-res screen size
float fogDensity;
float scatteringG;        // Henyey-Greenstein asymmetry parameter
float shadowBias;
float shadowMapSize;      // Per-cascade resolution
float maxDistance;
float _pad2;
```

Features:
- **32-step ray march** from camera to scene geometry (or `maxDistance`)
- **Henyey-Greenstein phase function** for directional scattering
- **Cascade-aware shadow atlas sampling** (same 2x2 grid layout as PBR)
- **Dithered start position** to reduce banding artifacts
- **Beer-Lambert transmittance** accumulation: `exp(-fogDensity * stepLength)`
- Output alpha = `1 - transmittance` (how much light was scattered)

---

#### `brdf_lut.comp` -- BRDF Integration LUT

Precomputes the split-sum BRDF integration lookup table for IBL. Run once at initialization.

**Bindings:**

| Set | Binding | Type                 | Name     | Description               |
|-----|---------|----------------------|----------|---------------------------|
| 0   | 0       | `image2D (rg16f)`    | `outLUT` | Output 512x512 BRDF LUT   |

No sampler inputs. No uniform buffers.

**Output Format:** RG16F where R = Fresnel scale, G = Fresnel bias.

**Algorithm:**
- 1024 importance-sampled GGX hemisphere samples per texel
- X axis = NdotV (view angle), Y axis = roughness
- Uses Hammersley low-discrepancy sequence
- IBL geometry term: k = roughness^2 / 2 (different from direct lighting)

Dispatch: `(512/16, 512/16, 1)` = `(32, 32, 1)`.

---

#### `irradiance.comp` -- Diffuse Irradiance Cubemap

Convolves the environment into a diffuse irradiance cubemap via Monte Carlo hemisphere integration. Each texel stores the cosine-weighted integral of incoming radiance over the aligned hemisphere.

**Bindings:**

| Set | Binding | Type                       | Name            | Description           |
|-----|---------|----------------------------|-----------------|-----------------------|
| 0   | 0       | `imageCube (rgba16f)`      | `outIrradiance` | Output cubemap        |

No sampler inputs -- evaluates the **procedural sky** inline (same math as `skybox.frag`), avoiding the need to render the sky to a cubemap first.

Uses uniform hemisphere sampling with `deltaPhi = 0.025`, `deltaTheta = 0.025` (~25,000 samples per texel).

Dispatch: `(faceSize/16, faceSize/16, 6)`.

---

#### `prefilter.comp` -- Specular Pre-filter Cubemap

Pre-filters the environment map for specular IBL using importance-sampled GGX. Each mip level corresponds to a roughness value (mip 0 = mirror, max mip = fully rough).

**Bindings:**

| Set | Binding | Type                    | Name            | Description           |
|-----|---------|-------------------------|-----------------|-----------------------|
| 0   | 0       | `imageCube (rgba16f)`   | `outPrefilter`  | Output cubemap mip    |
| 1   | 0       | `uniform PrefilterParams` | --            | `roughness` for this mip |

**`PrefilterParams`:**

```glsl
float roughness;
float _pad1, _pad2, _pad3;
```

Evaluates the procedural sky inline (same as irradiance). 512 importance-sampled GGX rays per texel using Hammersley sequence. Dispatched once per mip level.

---

### 3. Particle / Effect Shaders

All particle/effect shaders share a common **`ParticleUniforms`** block at `set 1, binding 0`:

```glsl
layout(set = 1, binding = 0) uniform ParticleUniforms {
    mat4  view;
    mat4  proj;
    vec3  camPos;   float _p0;
    vec3  camRight; float _p1;
    vec3  camUp;    float _p2;
};
```

Total size: 176 bytes. Contains the camera view/projection matrices plus camera basis vectors for billboarding and orientation calculations.

---

#### `particle_billboard.vert` / `particle_billboard.frag` -- GPU Billboard Particles

Camera-facing billboard particles for sparks, impact flashes, and debris. Velocity-oriented streaking for fast particles.

**Storage Buffer (set 0, binding 0) -- `ParticleData`:**

`BillboardParticle` layout (48 bytes = 12 floats per particle):

| Offset | Type   | Field    | Description                   |
|--------|--------|----------|-------------------------------|
| 0--2   | `vec3` | pos      | World-space position          |
| 3      | `float`| size     | Billboard half-size           |
| 4--7   | `vec4` | color    | RGBA color                    |
| 8--10  | `vec3` | vel      | Velocity                      |
| 11     | `float`| lifetime | Remaining lifetime            |

Accessed via `gl_InstanceIndex * 12` into a flat `float[]` buffer.

**Vertex Shader Outputs:**

| Location | Type    | Name         | Description                           |
|----------|---------|--------------|---------------------------------------|
| 0        | `vec2`  | `vUV`        | Quad corner coordinates (-1..+1)      |
| 1        | `vec4`  | `vColor`     | Particle color                        |
| 2        | `float` | `vSpeedNorm` | Normalized speed (0=slow/circle, 1=fast/streak) |

**Behavior:**
- Speed > 20 u/s: velocity-oriented elongated streak (length scales with speed)
- Speed <= 20 u/s: spherical camera-facing billboard

**Fragment Shader:**
- Fast particles: streak shape with cross-section fade, tail-to-tip brightness gradient, white-hot centerline core
- Slow particles: soft circular disc with bright center hotspot

---

#### `smoke.vert` / `smoke.frag` -- Volumetric Smoke

Camera-facing billboard smoke particles with per-particle rotation and noise-based dissolve.

**Storage Buffer (set 0, binding 0) -- `SmokeData`:**

`SmokeParticle` layout (48 bytes = 12 floats per particle):

| Offset | Type    | Field          | Description                |
|--------|---------|----------------|----------------------------|
| 0--2   | `vec3`  | pos            | World-space position       |
| 3      | `float` | size           | Billboard half-size        |
| 4--7   | `vec4`  | color          | Pre-multiplied RGBA        |
| 8      | `float` | rotation       | Rotation angle (radians)   |
| 9      | `float` | normalizedAge  | 0.0 = birth, 1.0 = death  |
| 10     | `float` | maxLifetime    | Maximum lifetime           |
| 11     | `float` | _pad           | Padding                    |

**Vertex Shader:**
- Applies per-particle rotation around the camera-forward axis using 2D rotation matrix on corner coordinates.

**Fragment Shader Texture (set 2, binding 0):**

| Binding | Type        | Name         | Description         |
|---------|-------------|--------------|---------------------|
| 0       | `sampler2D` | `smokeNoise` | Noise texture for dissolve |

Fragment output: `vColor * noiseMask * radialFalloff`. The noise UV is animated by the particle age for a dissolving/churning effect.

---

#### `ribbon.vert` / `ribbon.frag` -- Ribbon Trail Strips

Pre-expanded ribbon geometry using a vertex buffer (not storage buffer). Used for motion trails.

**Vertex Inputs:**

| Location | Type   | Name      | Description                       |
|----------|--------|-----------|-----------------------------------|
| 0        | `vec4` | `inPosP`  | xyz = world position, w = padding |
| 1        | `vec4` | `inColor` | Pre-multiplied alpha color        |

32 bytes per vertex (`RibbonVertex`).

**Fragment Shader:** Direct pass-through of `vColor`. All alpha and color are baked by the CPU.

---

#### `tracer.vert` / `tracer.frag` -- Bullet Tracer Capsules

Oriented camera-facing quads for bullet tracers, stretched from tail to tip.

**Storage Buffer (set 0, binding 0) -- `TracerData`:**

`TracerParticle` layout (80 bytes = 20 floats per particle):

| Offset  | Type   | Field      | Description               |
|---------|--------|------------|---------------------------|
| 0--2    | `vec3` | tip        | Tip world position        |
| 3       | `float`| radius     | Cross-section radius      |
| 4--6    | `vec3` | tail       | Tail world position       |
| 7       | `float`| brightness | Overall brightness        |
| 8--11   | `vec4` | coreColor  | Inner core color          |
| 12--15  | `vec4` | edgeColor  | Outer glow color          |
| 16      | `float`| lifetime   | Remaining lifetime        |
| 17--19  | pad    | --         | Padding                   |

**Vertex Shader Outputs:**

| Location | Type    | Name          | Description                              |
|----------|---------|---------------|------------------------------------------|
| 0        | `vec2`  | `vUV`         | x=0(tail)..1(tip), y=-1..+1 (cross)     |
| 1        | `float` | `vBrightness` | Per-particle brightness                  |
| 2        | `vec4`  | `vCoreColor`  | Inner core color                         |
| 3        | `vec4`  | `vEdgeColor`  | Outer glow color                         |

**Fragment Shader:** Three-layer cross-section profile:
- Core: `smoothstep(0, 0.10)` -- white-hot center line
- Mid: `smoothstep(0, 0.40)` -- orange glow
- Glow: `smoothstep(0, 1.00)` -- broad diffuse aura
- Tip-to-tail brightness falloff: `0.4 + 0.6 * u`
- Overdriven white-hot core for HDR bloom effect

---

#### `hitscan_beam.vert` / `hitscan_beam.frag` -- Energy Beam Effects

Oriented camera-facing quads for instant hitscan weapon beams (origin to hit position).

**Storage Buffer (set 0, binding 0) -- `BeamData`:**

`HitscanBeam` layout (64 bytes = 16 floats per beam):

| Offset  | Type   | Field      | Description               |
|---------|--------|------------|---------------------------|
| 0--2    | `vec3` | origin     | Beam start (weapon muzzle)|
| 3       | `float`| radius     | Beam radius               |
| 4--6    | `vec3` | hitPos     | Beam end (hit point)      |
| 7       | `float`| lifetime   | Remaining lifetime        |
| 8--11   | `vec4` | coreColor  | Inner core color          |
| 12--15  | `vec4` | edgeColor  | Outer glow color          |

**Vertex Shader:** Same oriented-quad technique as tracers. Brightness derived from lifetime: `clamp(lifetime / 0.12, 0, 1)` (fades over 120ms).

**Fragment Shader:** Three-layer beam profile:
- Outer glow: `pow(t, 2)` -- wide energy aura
- Inner glow: `pow(t, 6)` -- concentrated channel
- Core: `pow(t, 20)` -- white-hot centerline
- Overdriven `(1.0, 1.2, 1.4) * 3.0` for HDR bloom

---

#### `lightning_arc.vert` / `lightning_arc.frag` -- Lightning Arc Strips

Pre-expanded strip geometry for electrical arcs (e.g., grapple hook cable).

**Vertex Inputs:**

| Location | Type   | Name        | Description                              |
|----------|--------|-------------|------------------------------------------|
| 0        | `vec4` | `inPosEdge` | xyz = world position, w = edge (-1..+1)  |
| 1        | `vec4` | `inColor`   | rgb = hue, a = per-strip base alpha      |

32 bytes per vertex (`ArcVertex`).

**Vertex Shader Outputs:**

| Location | Type    | Name    | Description                     |
|----------|---------|---------|---------------------------------|
| 0        | `float` | `vEdge` | Strip edge position (-1..+1)    |
| 1        | `vec4`  | `vColor`| Color with alpha channel        |

**Fragment Shader:** Three-layer cross-section with three rendering layers (bloom, glow, core) controlled by `vColor.a`:
- `gaussGlow`: Broad Gaussian halo `exp(-d^2 * 5)` -- atmospheric scatter
- `innerGlow`: Tight Gaussian `exp(-d^2 * 28)` -- energy channel, blue-white `(0.50, 0.82, 1.00)`
- `spike`: Sub-pixel power-law `pow(1 - d*18, 3)` -- white-hot centerline

The `vColor.a` scales the inner layers: bloom layer (a~0.07) stays dim, core layer (a~0.96) blows out to white.

---

#### `decal.vert` / `decal.frag` -- Projected Decals

Camera-facing projected quads for bullet hole decals sampled from a texture atlas.

**Storage Buffer (set 0, binding 0) -- `DecalData`:**

`DecalInstance` layout (64 bytes = 16 floats per decal):

| Offset  | Type   | Field    | Description                    |
|---------|--------|----------|--------------------------------|
| 0--2    | `vec3` | pos      | Decal center (world space)     |
| 3       | `float`| size     | Decal half-size                |
| 4--6    | `vec3` | right    | Right basis vector             |
| 7       | `float`| _p0      | Padding                        |
| 8--10   | `vec3` | up       | Up basis vector                |
| 11      | `float`| opacity  | Fade opacity (ages toward 0)   |
| 12--13  | `vec2` | uvMin    | Atlas UV min                   |
| 14--15  | `vec2` | uvMax    | Atlas UV max                   |

**Vertex Shader Outputs:**

| Location | Type    | Name       | Description           |
|----------|---------|------------|-----------------------|
| 0        | `vec2`  | `vUV`      | Atlas UV coordinate   |
| 1        | `float` | `vOpacity` | Per-decal fade        |

**Fragment Shader Texture (set 2, binding 0):**

| Binding | Type        | Name         | Description      |
|---------|-------------|--------------|------------------|
| 0       | `sampler2D` | `decalAtlas` | Decal atlas texture |

Alpha = `texture.a * opacity`. Fragments with alpha < 0.01 are discarded to avoid wasting fill rate.

---

#### `sdf_text.vert` / `sdf_text.frag` -- SDF Text Rendering

Signed distance field text rendering for both world-space text and HUD overlays.

**Storage Buffer (set 0, binding 0) -- `GlyphData`:**

`SdfGlyphGPU` layout (80 bytes = 20 floats per glyph):

| Offset  | Type   | Field     | Description                   |
|---------|--------|-----------|-------------------------------|
| 0--2    | `vec3` | worldPos  | Glyph origin (world space)    |
| 3       | `float`| size      | Glyph height                  |
| 4--5    | `vec2` | uvMin     | Atlas UV min                  |
| 6--7    | `vec2` | uvMax     | Atlas UV max                  |
| 8--11   | `vec4` | color     | RGBA color                    |
| 12--14  | `vec3` | right     | Right basis vector            |
| 15      | `float`| _p0       | Padding                       |
| 16--18  | `vec3` | up        | Up basis vector               |
| 19      | `float`| _p1       | Padding                       |

**Vertex Shader:** Computes glyph aspect ratio from UV region (`uvSize.x / uvSize.y`) and scales width accordingly. Uses the `ParticleUniforms` block for world-space text; the same binding slot holds a simpler `HudUniforms` (invScreenSize) for HUD text.

**Fragment Shader Texture (set 2, binding 0):**

| Binding | Type        | Name       | Description       |
|---------|-------------|------------|-------------------|
| 0       | `sampler2D` | `sdfAtlas` | SDF font atlas    |

Anti-aliased edge rendering: `alpha = smoothstep(0.5 - w, 0.5 + w, sdf)` where `w = fwidth(sdf) * 0.7`. The `fwidth` function provides screen-space derivatives for resolution-independent smooth edges.

---

### 4. Transparency Shaders

#### `pbr_wboit.frag` -- Weighted Blended OIT (Accumulation Pass)

Weighted Blended Order-Independent Transparency accumulation pass. Uses the same PBR lighting setup as `pbr.frag` but writes to dual render targets for OIT compositing.

**Inputs:** Same as `pbr.frag` (locations 0--4), paired with `pbr.vert`.

**Outputs (MRT):**

| Location | Type   | Name            | Format   | Description                     |
|----------|--------|-----------------|----------|---------------------------------|
| 0        | `vec4` | `accumulation`  | RGBA16F  | Premultiplied color * weight    |
| 1        | `vec4` | `revealage`     | R8       | Alpha (product of 1 - alpha)   |

**Texture Samplers (set 2):**

| Binding | Type        | Name        | Description          |
|---------|-------------|-------------|----------------------|
| 0       | `sampler2D` | `texAlbedo` | Base-color texture   |

**Uniform Buffer (set 3, binding 0) -- `Material`:** Same `Material` struct as `pbr.frag`.

**Weight function** (McGuire & Bavoil 2013):
```glsl
weight = alpha * max(0.01, 3000.0 * pow(1.0 - gl_FragCoord.z, 3.0))
```
Biases by depth so nearer transparent surfaces have more influence.

---

#### `oit_resolve.frag` -- OIT Resolve Pass

Fullscreen resolve that composites the OIT accumulation + revealage buffers onto the opaque background. Paired with `fullscreen.vert`.

**Texture Samplers (set 2):**

| Binding | Type        | Name            | Description           |
|---------|-------------|-----------------|-----------------------|
| 0       | `sampler2D` | `accumTexture`  | Accumulation buffer   |
| 1       | `sampler2D` | `revealTexture` | Revealage buffer      |

**Output:** `vec4(avgColor, 1.0 - reveal)` -- weighted average color blended over the background using the revealage as alpha. Discards pixels with no transparent fragments (`accum.a < 0.001`).

---

## Uniform Buffer Layouts

### `Matrices` (Vertex Shaders -- set 1, binding 0)

Used by: `pbr.vert`, `model.vert`, `projective.vert`, `geometry.vert`

| Field          | Type   | Size     | Offset | Notes                              |
|----------------|--------|----------|--------|------------------------------------|
| `model`        | `mat4` | 64 bytes | 0      | Object-to-world transform          |
| `view`         | `mat4` | 64 bytes | 64     | World-to-view transform            |
| `projection`   | `mat4` | 64 bytes | 128    | View-to-clip transform             |
| `normalMatrix` | `mat4` | 64 bytes | 192    | transpose(inverse(model)), PBR only |

Total: 192 bytes (simple) or 256 bytes (PBR with `normalMatrix`).

### `Material` (Fragment Shaders -- set 3, binding 0)

Used by: `pbr.frag`, `pbr_wboit.frag`

| Field              | Type    | Offset | Description                        |
|--------------------|---------|--------|------------------------------------|
| `baseColorFactor`  | `vec4`  | 0      | Base color multiplier (RGBA)       |
| `metallicFactor`   | `float` | 16     | Metallic multiplier                |
| `roughnessFactor`  | `float` | 20     | Roughness multiplier               |
| `aoStrength`       | `float` | 24     | Ambient occlusion strength         |
| `normalScale`      | `float` | 28     | Normal map intensity               |
| `emissiveFactor`   | `vec4`  | 32     | Emissive color (rgb), w unused     |

Total: 48 bytes.

### `LightData` (Fragment Shaders -- set 3, binding 1)

Used by: `pbr.frag`

| Field          | Type       | Offset | Description                   |
|----------------|------------|--------|-------------------------------|
| `cameraPos`    | `vec4`     | 0      | World-space eye position      |
| `ambientColor` | `vec4`     | 16     | Ambient radiance (rgb)        |
| `numLights`    | `int`      | 32     | Number of active lights       |
| `_pad1..3`     | `float`x3  | 36     | Padding                       |
| `lights[8]`    | `Light`x8  | 48     | Light array                   |

`Light` struct (48 bytes): `position(16) + color(16) + params(16)`.

Total: 48 + 8 * 48 = 432 bytes.

### `ShadowData` (Fragment Shaders -- set 3, binding 2)

Used by: `pbr.frag`

| Field              | Type      | Offset | Description                           |
|--------------------|-----------|--------|---------------------------------------|
| `lightVP[4]`       | `mat4`x4  | 0      | Per-cascade light VP matrices         |
| `cascadeSplits`    | `vec4`    | 256    | View-space far distances              |
| `cameraView`       | `mat4`    | 272    | Camera view matrix                    |
| `shadowBias`       | `float`   | 336    | Depth bias                            |
| `shadowNormalBias` | `float`   | 340    | Normal-offset bias                    |
| `shadowMapSize`    | `float`   | 344    | Per-cascade resolution                |
| `_shadowPad`       | `float`   | 348    | Padding                               |

Total: 352 bytes.

### `TonemapParams` (Fragment Shaders -- set 3, binding 0)

Used by: `tonemap.frag`

| Field                | Type    | Offset | Description                    |
|----------------------|---------|--------|--------------------------------|
| `exposure`           | `float` | 0      | Exposure multiplier            |
| `gamma`              | `float` | 4      | Gamma correction value         |
| `tonemapMode`        | `int`   | 8      | 0=ACES, 1=Reinhard, 2=Linear  |
| `bloomStrength`      | `float` | 12     | Bloom blend strength           |
| `ssaoStrength`       | `float` | 16     | SSAO blend strength            |
| `ssrStrength`        | `float` | 20     | SSR blend strength             |
| `volumetricStrength` | `float` | 24     | Volumetric blend strength      |
| `sharpenStrength`    | `float` | 28     | Post-TAA sharpening intensity  |

Total: 32 bytes.

### `ParticleUniforms` (Vertex Shaders -- set 1, binding 0)

Used by: all particle/effect shaders

| Field      | Type   | Offset | Description                 |
|------------|--------|--------|-----------------------------|
| `view`     | `mat4` | 0      | Camera view matrix          |
| `proj`     | `mat4` | 64     | Projection matrix           |
| `camPos`   | `vec3` | 128    | Camera world position       |
| `_p0`      | `float`| 140    | Padding                     |
| `camRight` | `vec3` | 144    | Camera right vector         |
| `_p1`      | `float`| 156    | Padding                     |
| `camUp`    | `vec3` | 160    | Camera up vector            |
| `_p2`      | `float`| 172    | Padding                     |

Total: 176 bytes.

---

## Binding Conventions

### Descriptor Set Assignments

The SDL3 GPU API dictates fixed descriptor set mappings per shader stage:

**Vertex Shaders:**
- `set 0` -- Storage buffers (particle data) and storage textures
- `set 1` -- Uniform buffers (Matrices, ParticleUniforms, LightMatrices)

**Fragment Shaders:**
- `set 2` -- Sampled textures (albedo, normal, shadow maps, post-processing inputs)
- `set 3` -- Uniform buffers (Material, LightData, ShadowData, TonemapParams)

**Compute Shaders:**
- `set 0` -- Sampled textures (read-only inputs: depth, HDR, shadow maps)
- `set 1` -- Storage images (write-only or read-write outputs)
- `set 2` -- Uniform buffers (per-effect parameters)

### Common Binding Patterns

| Shader Type      | set 0           | set 1             | set 2           | set 3           |
|------------------|-----------------|--------------------|-----------------|-----------------|
| PBR              | --              | Matrices UBO       | 8 textures      | Material + Light + Shadow UBOs |
| Simple model     | --              | Matrices UBO       | 1 diffuse tex   | --              |
| Scene geometry   | --              | Matrices UBO       | Shadow map      | SceneShadowData UBO |
| Shadow pass      | --              | LightMatrices UBO  | --              | --              |
| Skybox           | --              | SkyboxMatrices UBO | Environment cube| SkyboxParams UBO|
| Post-process     | --              | --                 | Input textures  | Effect params UBO |
| Compute effect   | Input textures  | Output image       | Params UBO      | --              |
| Particle effect  | Storage buffer  | ParticleUniforms   | Atlas textures  | --              |

---

## Storage Buffer Patterns

### Particle Instance Data

All GPU particle effects use the same pattern:

1. **CPU-side:** Particle data is packed into a flat `float[]` array and uploaded to a GPU storage buffer each frame.

2. **Vertex shader:** Declares `readonly buffer` at `set 0, binding 0`:
   ```glsl
   layout(set = 0, binding = 0) readonly buffer ParticleData { float data[]; };
   ```

3. **Indexing:** Each particle's data is accessed via `gl_InstanceIndex`:
   ```glsl
   const int stride = <floats_per_particle>;
   const int base   = gl_InstanceIndex * stride;
   vec3 pos = vec3(data[base+0], data[base+1], data[base+2]);
   ```

4. **Quad generation:** 4 vertices per particle, indexed by `gl_VertexIndex % 4`:
   ```glsl
   const vec2 corners[4] = vec2[](
       vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,1)
   );
   vec2 c = corners[gl_VertexIndex % 4];
   ```

5. **Draw call:** `drawIndexed(6, particleCount)` with a shared quad index buffer `[0,1,2, 0,2,3]`.

### Particle Type Strides

| Effect             | Struct Name       | Bytes | Floats (stride) |
|--------------------|-------------------|-------|-----------------|
| Billboard sparks   | `BillboardParticle`| 48   | 12              |
| Smoke puffs        | `SmokeParticle`   | 48    | 12              |
| Bullet tracers     | `TracerParticle`  | 80    | 20              |
| Hitscan beams      | `HitscanBeam`     | 64    | 16              |
| Decals             | `DecalInstance`   | 64    | 16              |
| SDF text glyphs    | `SdfGlyphGPU`     | 80    | 20              |

### Vertex Buffer Effects

Some effects use pre-expanded vertex buffers instead of storage buffers:

| Effect          | Vertex Struct   | Bytes | Inputs                          |
|-----------------|-----------------|-------|---------------------------------|
| Ribbon trails   | `RibbonVertex`  | 32    | `vec4 pos + vec4 color`         |
| Lightning arcs  | `ArcVertex`     | 32    | `vec4 posEdge + vec4 color`     |

These are drawn as triangle strips with CPU-generated vertices uploaded per frame. Color and alpha are fully baked on the CPU side.
