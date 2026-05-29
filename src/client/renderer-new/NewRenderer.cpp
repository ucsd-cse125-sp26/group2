/// @file NewRenderer.cpp
/// @brief Implementation of the work-in-progress NewRenderer.
///
/// Grep for `TODO(graphics)` to find every stub still waiting on the
/// graphics team.  Each TODO has a doc-comment block on the matching
/// header declaration describing what data is captured and where it
/// should end up.

#include "NewRenderer.hpp"

#include "Asset.hpp"
#include "AssetLoader.hpp"
#include "Boilerplate.hpp"
#include "particles/ParticleSystem.hpp"

#include <algorithm>
#include <backends/imgui_impl_sdlgpu3.h>
#include <cmath>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <imgui.h>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>

namespace
{
/// Convert SDL high-resolution counter ticks to milliseconds.
float ticksToMs(Uint64 elapsed, Uint64 freq)
{
    return freq == 0 ? 0.0f : (static_cast<float>(elapsed) * 1000.0f) / static_cast<float>(freq);
}

/// Side-table for per-model emissive overrides set via `setModelEmissive`.
/// TODO(graphics): consume these inside `drawModel` when building the
/// material UBO — currently captured but unused.
std::unordered_map<int32_t, glm::vec4> g_emissiveOverrides;

float verticalFovDegreesFromHorizontal(float horizontalFovDegrees, float aspect)
{
    const float safeAspect = aspect > 0.0f ? aspect : 1.0f;
    const float horizontalRadians = glm::radians(horizontalFovDegrees);
    return glm::degrees(2.0f * std::atan(std::tan(horizontalRadians * 0.5f) / safeAspect));
}
} // namespace

// ─── Lifecycle ───────────────────────────────────────────────────────────────

bool NewRenderer::init(SDL_Window* window)
{
    window_ = window;

    constexpr SDL_GPUShaderFormat k_wantedFormats = SDL_GPU_SHADERFORMAT_SPIRV
#ifdef HAVE_MSL_SHADERS
                                                    | SDL_GPU_SHADERFORMAT_MSL
#endif
#ifdef HAVE_DXIL_SHADERS
                                                    | SDL_GPU_SHADERFORMAT_DXIL
#endif
        ;
    device_ = SDL_CreateGPUDevice(k_wantedFormats, true, nullptr);
    if (!device_) {
        SDL_Log("NewRenderer: SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    SDL_Log("NewRenderer: GPU driver = %s", SDL_GetGPUDeviceDriver(device_));
    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
        SDL_Log("NewRenderer: SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    // Allow the CPU to run up to 3 frames ahead of the GPU (default 2).  With an
    // uncapped present mode this lets the swapchain acquire return without
    // blocking far more often, which keeps the render loop from stalling on
    // GPU back-pressure.
    SDL_SetGPUAllowedFramesInFlight(device_, 3);

    ImGui_ImplSDLGPU3_InitInfo imguiInfo = Boilerplate::createImGuiInfo(device_, window_);
    if (!ImGui_ImplSDLGPU3_Init(&imguiInfo)) {
        SDL_Log("NewRenderer: ImGui_ImplSDLGPU3_Init failed");
        return false;
    }

    colorTarget_ = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    shaderFormat_ = Boilerplate::selectShaderFormat(device_);
    if (shaderFormat_ == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_Log("NewRenderer: no supported shader format (got 0x%x)",
                static_cast<unsigned>(SDL_GetGPUShaderFormats(device_)));
        return false;
    }

    if (!createGeometryPipeline()) {
        SDL_Log("NewRenderer: failed to create geometry pipeline: %s", SDL_GetError());
        return false;
    }

    if (!createHudPipeline()) {
        SDL_Log("NewRenderer: failed to create hud pipeline: %s", SDL_GetError());
        return false;
    }

    if (!createDepthPipeline()) {
        SDL_Log("NewRenderer: failed to create depth pipeline: %s", SDL_GetError());
        return false;
    }

    if (!createFxaaPipeline()) {
        SDL_Log("NewRenderer: failed to create FXAA pipeline: %s", SDL_GetError());
        return false;
    }

    sampler_ = Boilerplate::createLinearRepeatSampler(device_);
    if (!sampler_) {
        SDL_Log("NewRenderer: failed to create sampler: %s", SDL_GetError());
        return false;
    }

    hudSampler_ = Boilerplate::createLinearRepeatSampler(device_);
    if (!hudSampler_) {
        SDL_Log("NewRenderer: failed to create hud sampler: %s", SDL_GetError());
        return false;
    }

    depthSampler_ = Boilerplate::createLinearComparisonSampler(device_);
    if (!depthSampler_) {
        SDL_Log("NewRenderer: failed to create depth sampler: %s", SDL_GetError());
        return false;
    }

    fxaaSampler_ = Boilerplate::createLinearClampSampler(device_);
    if (!fxaaSampler_) {
        SDL_Log("NewRenderer: failed to create FXAA sampler: %s", SDL_GetError());
        return false;
    }

    // DEFAULT TEXTURE — used for any mesh whose material has no albedo.
    texture_ = Boilerplate::loadTexture(device_, "assets/404.jpeg");
    if (!texture_) {
        SDL_Log("NewRenderer: failed to load texture");
        return false;
    }

    camera_ = NewCamera();

    skinnedRenderer_.init(device_, colorTarget_, shaderFormat_);

    return true;
}

bool NewRenderer::createHudPipeline()
{
    Boilerplate::ShaderInfo vertexShader{};
    vertexShader.path = "shaders-new/hud.vert";
    vertexShader.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexShader.uniformBufferCount = 2;

    Boilerplate::ShaderInfo fragmentShader{};
    fragmentShader.path = "shaders-new/hud.frag";
    fragmentShader.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentShader.samplerCount = 1;

    const Boilerplate::VertexInputLayout vertexLayout{};

    hudPipeline_ = Boilerplate::createGraphicsPipeline(
        device_, colorTarget_, shaderFormat_, vertexShader, fragmentShader, vertexLayout, false, true);

    return hudPipeline_ != nullptr;
}

bool NewRenderer::createFxaaPipeline()
{
    Boilerplate::ShaderInfo vertexShader{};
    vertexShader.path = "shaders-new/hud.vert";
    vertexShader.stage = SDL_GPU_SHADERSTAGE_VERTEX;

    Boilerplate::ShaderInfo fragmentShader{};
    fragmentShader.path = "shaders-new/fxaa.frag";
    fragmentShader.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentShader.samplerCount = 1;
    fragmentShader.uniformBufferCount = 1;

    const Boilerplate::VertexInputLayout vertexLayout{};

    fxaaPipeline_ = Boilerplate::createGraphicsPipeline(
        device_, colorTarget_, shaderFormat_, vertexShader, fragmentShader, vertexLayout, false, false);

    return fxaaPipeline_ != nullptr;
}

bool NewRenderer::createGeometryPipeline()
{
    Boilerplate::ShaderInfo vertexShader{};
    vertexShader.path = "shaders-new/geometry.vert";
    vertexShader.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexShader.uniformBufferCount = 2;

    Boilerplate::ShaderInfo fragmentShader{};
    fragmentShader.path = "shaders-new/geometry_shadowed.frag";
    fragmentShader.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentShader.samplerCount = 4;
    fragmentShader.uniformBufferCount = 3;

    SDL_GPUVertexBufferDescription vertexBufferDescription{};
    vertexBufferDescription.slot = 0;
    vertexBufferDescription.pitch = (sizeof(Vertex));
    vertexBufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertexBufferDescription.instance_step_rate = 0;

    Boilerplate::VertexInputLayout vertexLayout{};
    // vertexLayout.vertexPitch = sizeof(Vertex);
    vertexLayout.bufferDescriptions.push_back(vertexBufferDescription);
    vertexLayout.attributes = {
        Boilerplate::makeAttribute(0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, position)),
        Boilerplate::makeAttribute(1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, normal)),
        Boilerplate::makeAttribute(2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, texUV)),
        Boilerplate::makeAttribute(3, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex, tangent)),
    };

    geometryPipeline_ = Boilerplate::createGraphicsPipeline(
        device_, colorTarget_, shaderFormat_, vertexShader, fragmentShader, vertexLayout, true, true);

    return geometryPipeline_ != nullptr;
}

bool NewRenderer::createDepthPipeline()
{
    Boilerplate::ShaderInfo vertexShader{};
    vertexShader.path = "shaders-new/geometry_depth.vert";
    vertexShader.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexShader.uniformBufferCount = 2;

    Boilerplate::ShaderInfo fragmentShader{};
    fragmentShader.path = "shaders-new/emtpy.frag";
    fragmentShader.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentShader.uniformBufferCount = 0;

    SDL_GPUVertexBufferDescription vertexBufferDescription{};
    vertexBufferDescription.slot = 0;
    vertexBufferDescription.pitch = (sizeof(Vertex));
    vertexBufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertexBufferDescription.instance_step_rate = 0;

    Boilerplate::VertexInputLayout vertexLayout{};
    // vertexLayout.vertexPitch = sizeof(Vertex);
    vertexLayout.bufferDescriptions.push_back(vertexBufferDescription);
    vertexLayout.attributes = {
        Boilerplate::makeAttribute(0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, position)),
        Boilerplate::makeAttribute(1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, normal)),
        Boilerplate::makeAttribute(2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, texUV)),
    };

    Boilerplate::PipelineDescription depthPipelineDesc{};
    depthPipelineDesc.vertexShaderInfo = &vertexShader;
    depthPipelineDesc.fragmentShaderInfo = &fragmentShader;
    depthPipelineDesc.shaderFormat = shaderFormat_;
    depthPipelineDesc.vertexInputLayout = &vertexLayout;
    depthPipelineDesc.colorTarget = nullptr;
    depthPipelineDesc.depthTest = true;
    depthPipelineDesc.depthWrite = true;
    // depthPipelineDesc.cullMode = SDL_GPU_CULLMODE_BACK;
    // depthPipelineDesc.cullMode = SDL_GPU_CULLMODE_FRONT;
    depthPipelineDesc.cullMode = SDL_GPU_CULLMODE_NONE;

    depthPipeline_ = Boilerplate::createGraphicsDepthPipeline(device_, depthPipelineDesc);

    return depthPipeline_ != nullptr;
}

void NewRenderer::createMeshBuffers(MeshIdInt meshId) const
{
    Asset::Mesh& mesh = Asset::meshes_.at(meshId);

    const size_t vertexBufferSize = mesh.vertexData_.size() * sizeof(Vertex);
    const size_t indexBufferSize = mesh.indexData_.size() * sizeof(Uint32);

    mesh.vBufferInfo_.bufferSize = static_cast<Uint32>(vertexBufferSize);
    mesh.vBufferInfo_.gpuBuff = Boilerplate::createBuffer(device_, vertexBufferSize, SDL_GPU_BUFFERUSAGE_VERTEX);
    mesh.vBufferInfo_.srcData = mesh.vertexData_.data();

    mesh.iBufferInfo_.bufferSize = static_cast<Uint32>(indexBufferSize);
    mesh.iBufferInfo_.gpuBuff = Boilerplate::createBuffer(device_, indexBufferSize, SDL_GPU_BUFFERUSAGE_INDEX);
    mesh.iBufferInfo_.srcData = mesh.indexData_.data();
}

// ─── Per-frame entry point ──────────────────────────────────────────────────

void NewRenderer::drawFrame(glm::vec3 eye, float yaw, float pitch, float roll)
{

    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 t0 = SDL_GetPerformanceCounter();

    // TEMP ablation profiling: env-gated per-pass skips.  We have no GPU
    // timestamp API (SDL3 GPU exposes only fences), and we are GPU/present-bound
    // (the CPU blocks in swapchain acquire), so disabling a pass frees the
    // swapchain sooner and the presented-FPS delta = that pass's GPU cost.
    static const bool kSkipShadow = SDL_getenv("GROUP2_PROF_NO_SHADOW") != nullptr;
    static const bool kSkipGeom = SDL_getenv("GROUP2_PROF_NO_GEOM") != nullptr;
    static const bool kSkipWeapon = SDL_getenv("GROUP2_PROF_NO_WEAPON") != nullptr;
    static const bool kSkipFxaa = SDL_getenv("GROUP2_PROF_NO_FXAA") != nullptr;
    static const bool kSkipHud = SDL_getenv("GROUP2_PROF_NO_HUD") != nullptr;
    static const bool kSkipUI = SDL_getenv("GROUP2_PROF_NO_UI") != nullptr;

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) {
        SDL_Log("NewRenderer::drawFrame: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return;
    }
    const Uint64 t1 = SDL_GetPerformanceCounter();
    lastAcquireMs_ = ticksToMs(t1 - t0, freq);

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    const Uint64 tSwap0 = SDL_GetPerformanceCounter();
    if (!SDL_AcquireGPUSwapchainTexture(cmd, window_, &swapchain, &width, &height)) {
        SDL_Log("NewRenderer::drawFrame: SDL_AcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }
    const Uint64 tSwap1 = SDL_GetPerformanceCounter();

    if (!swapchain) {
        // Swapchain not ready (e.g. minimised) — drop the frame silently.
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

    if (!ensureDepthTextureSize(width, height) || !ensureSceneTextureSize(width, height)) {
        SDL_Log("NewRenderer::drawFrame: failed to size render targets");
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

    // Per-frame uploads (skinning palette/instances, etc.) happen BEFORE
    // the first render pass so the copy is sequenced ahead of the draws.
    const Uint64 tCopy0 = SDL_GetPerformanceCounter();
    {
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        if (copyPass) {
            skinnedRenderer_.uploadFrame(cmd, copyPass);
            SDL_EndGPUCopyPass(copyPass);
        }
    }
    const Uint64 tCopy1 = SDL_GetPerformanceCounter();

    // Rebuild the static world-geometry batches if the scene changed.  Must run
    // outside any render pass (uploadBuffers opens its own copy pass); rare.
    if (worldBatchesDirty_)
        rebuildWorldBatches(cmd);

    ///////////////////////////////////// SHADOWMAP CREATION /////////////////////////////////////////////
    constexpr uint32_t shadowSize = 1024;
    constexpr Uint8 numCubeFaces = 6;

    static const glm::vec3 cubeFaceTargets[numCubeFaces] = {
        glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0),
        glm::vec3(0, -1, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1),
    };
    static const glm::vec3 cubeFaceUps[numCubeFaces] = {
        glm::vec3(0, -1, 0), glm::vec3(0, -1, 0), glm::vec3(0, 0, 1),
        glm::vec3(0, 0, -1), glm::vec3(0, -1, 0), glm::vec3(0, -1, 0),
    };

    // Point lights are supplied by Game every frame via setPointLights().

    // Allocate the shadow cubemap ONCE and reuse it.  (Old code created and
    // released a D32F cube array every frame — ~1.2 ms of CPU + allocator
    // churn that drove the frame-time tail.)
    const Uint64 tShadowAlloc0 = SDL_GetPerformanceCounter();
    if (shadowMap_ == nullptr) {
        shadowMap_ = Boilerplate::createEmptyTextureD32F(device_, shadowSize, shadowSize, true, MAX_POINT_LIGHTS);
        shadowDirty_ = true;
    }
    const Uint64 tShadowAlloc1 = SDL_GetPerformanceCounter();

    // Regenerate shadow-map contents on a throttled cadence, AND spread the
    // refresh across frames one cube face at a time.  Recording all 6 faces (per
    // light) in a single frame is a ~7 ms full-scene depth re-record that lands
    // on whatever frame the 30 Hz timer fires — a spike that dominates the P95/P99
    // tail.  Instead we start a refresh cycle at 30 Hz and emit exactly one face
    // per frame until the cube is complete (~1.2 ms each), so no single frame
    // pays the whole cost.  The light/world are static, so a face-by-face refresh
    // is visually identical.
    const Uint32 totalFaces = sceneLightInfo_.numPointLights * numCubeFaces;
    const Uint64 shadowPeriodTicks =
        shadowUpdateHz_ > 0.0 ? static_cast<Uint64>(static_cast<double>(freq) / shadowUpdateHz_) : 0;

    if (!kSkipShadow && sceneLightInfo_.numPointLights >= 1 && !shadowRefreshActive_ &&
        (shadowDirty_ || tShadowAlloc1 - lastShadowUpdateTick_ >= shadowPeriodTicks)) {
        shadowRefreshActive_ = true;
        shadowFaceCursor_ = 0;
        shadowDirty_ = false;
        lastShadowUpdateTick_ = tShadowAlloc1; // schedule next cycle from refresh start
    }

    if (shadowRefreshActive_) {
        glm::mat4 shadowProjection = glm::perspective(
            glm::radians(90.0f), 1.0f, sceneLightInfo_.pointLightNearPlane, sceneLightInfo_.pointLightFarPlane);
        shadowProjection[1][1] *= -1;

        constexpr Uint32 kFacesPerFrame = 1;
        for (Uint32 n = 0; n < kFacesPerFrame && shadowFaceCursor_ < totalFaces; ++n, ++shadowFaceCursor_) {
            const Uint32 iLight = shadowFaceCursor_ / numCubeFaces;
            const Uint32 face = shadowFaceCursor_ % numCubeFaces;
            const glm::vec3 lightPos = sceneLightInfo_.pointLights[iLight].position;
            const glm::mat4 shadowView = glm::lookAt(lightPos, lightPos + cubeFaceTargets[face], cubeFaceUps[face]);
            drawGeometryDepthPass(shadowMap_, iLight * numCubeFaces + face, cmd, shadowProjection * shadowView);
        }

        if (shadowFaceCursor_ >= totalFaces)
            shadowRefreshActive_ = false;
    }

    const Uint64 tShadowPasses1 = SDL_GetPerformanceCounter();

    ///////////////////////////////////// SHADOWMAP CREATION /////////////////////////////////////////////

    float fov = 60.0f;
    setMainCamera(eye, yaw, pitch, roll, width, height, fov);

    const Uint64 tGeom0 = SDL_GetPerformanceCounter();
    if (!kSkipGeom)
        drawGeometryPass(sceneColor_, cmd);
    const Uint64 tGeom1 = SDL_GetPerformanceCounter();
    if (!kSkipWeapon)
        drawWeaponPass(sceneColor_, cmd);
    if (!kSkipFxaa)
        drawFxaaPass(sceneColor_, swapchain, cmd);
    if (!kSkipHud)
        drawHudPass(swapchain, cmd);
    if (!kSkipUI)
        drawUIPass(swapchain, cmd);

    const Uint64 t2 = SDL_GetPerformanceCounter();
    lastRecordMs_ = ticksToMs(t2 - t1, freq);

    // TEMP attribution: accumulate sub-pass costs and log averages periodically.
    {
        static double accShadowAlloc = 0.0, accShadowPasses = 0.0, accGeom = 0.0, accRest = 0.0, accRec = 0.0;
        static double accSwap = 0.0, accCopy = 0.0, accAcq = 0.0, accSub = 0.0;
        static int accN = 0;
        const double msSwap = ticksToMs(tSwap1 - tSwap0, freq);
        const double msCopy = ticksToMs(tCopy1 - tCopy0, freq);
        const double msShadowAlloc = ticksToMs(tShadowAlloc1 - tShadowAlloc0, freq);
        const double msShadowPasses = ticksToMs(tShadowPasses1 - tShadowAlloc1, freq);
        const double msGeom = ticksToMs(tGeom1 - tGeom0, freq);
        const double msRest = ticksToMs(t2 - tGeom1, freq);
        accSwap += msSwap;
        accCopy += msCopy;
        accShadowAlloc += msShadowAlloc;
        accShadowPasses += msShadowPasses;
        accGeom += msGeom;
        accRest += msRest;
        accRec += lastRecordMs_;
        accAcq += lastAcquireMs_;
        accSub += lastSubmitMs_;
        if (++accN >= 300) {
            SDL_Log("[attr] cmdAcq=%.3f rec=%.3f submit=%.3f | swapAcq=%.3f copy=%.3f shadowAlloc=%.3f "
                    "shadowPasses=%.3f geometry=%.3f rest(weapon+fxaa+hud+ui)=%.3f",
                    accAcq / accN, accRec / accN, accSub / accN, accSwap / accN, accCopy / accN, accShadowAlloc / accN,
                    accShadowPasses / accN, accGeom / accN, accRest / accN);
            accSwap = accCopy = accShadowAlloc = accShadowPasses = accGeom = accRest = accRec = accAcq = accSub = 0.0;
            accN = 0;
        }
    }

    SDL_SubmitGPUCommandBuffer(cmd);
    ++presentedFrameCount_; // a frame genuinely went to the swapchain this iterate

    const Uint64 t3 = SDL_GetPerformanceCounter();
    lastSubmitMs_ = ticksToMs(t3 - t2, freq);

    // TODO(graphics): if `pendingScreenshotPath_` is non-empty, schedule a
    // swapchain readback and write a PNG.  See `requestScreenshot` doc.
}

void NewRenderer::setMainCamera(
    glm::vec3 eye, float yaw, float pitch, float roll, Uint32 width, Uint32 height, float fov)
{
    camera_.setFov(fov);
    // camera_.setZNear(zNear);
    // camera_.setZFar(zFar);

    camera_.setEye(eye);
    camera_.setTarget(pitch, yaw, roll);
    camera_.setAspect(static_cast<float>(width), static_cast<float>(height));
    const float aspect = height == 0 ? 1.0f : static_cast<float>(width) / static_cast<float>(height);
    // Apply scope zoom by dividing the horizontal FOV. Standard game-engine
    // convention for an "Nx scope" — at 1.5x, a 90° FOV narrows to 60°.
    const float zoomedHorizFov = mainHorizontalFovDegrees / std::max(scopeZoom, 0.01f);
    camera_.setFov(verticalFovDegreesFromHorizontal(zoomedHorizFov, aspect));
    camera_.computeViewProjectionMatrix();
}

void NewRenderer::drawGeometryDepthPass(SDL_GPUTexture* depthTexture,
                                        Uint8 layer,
                                        SDL_GPUCommandBuffer* cmd,
                                        const glm::mat4& shadowViewProjection)
{
    SDL_GPUDepthStencilTargetInfo depthTarget = Boilerplate::makeDepthTarget(depthTexture, layer, true);

    SDL_GPURenderPass* geometryPass = SDL_BeginGPURenderPass(cmd, nullptr, 0, &depthTarget);
    SDL_BindGPUGraphicsPipeline(geometryPass, depthPipeline_);

    SDL_PushGPUVertexUniformData(cmd, 0, &shadowViewProjection, sizeof(glm::mat4));

    drawWorldModelInstances(geometryPass, cmd, true);
    drawEntityModels(geometryPass, cmd, true);

    // Rasterise the skinned player rig into the shadow map so the player
    // casts a shadow (the shadow view-projection is already at vertex slot 0).
    skinnedRenderer_.drawDepth(geometryPass, cmd);

    SDL_EndGPURenderPass(geometryPass);
}

void NewRenderer::bindLightShadowInfo(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd)
{
    SDL_GPUTextureSamplerBinding shadowMapBinding{shadowMap_, depthSampler_};
    SDL_BindGPUFragmentSamplers(renderPass, 1, &shadowMapBinding, 1);

    SDL_PushGPUFragmentUniformData(cmd, 2, &sceneLightInfo_, sizeof(LightUBO));
}
void NewRenderer::drawGeometryPass(SDL_GPUTexture* sceneColor, SDL_GPUCommandBuffer* cmd)
{
    const Uint64 gfreq = SDL_GetPerformanceFrequency();
    const Uint64 gt0 = SDL_GetPerformanceCounter();
    if (particleSystem_)
        particleSystem_->uploadToGpu(cmd); // Must be before render pass
    const Uint64 gt1 = SDL_GetPerformanceCounter();

    SDL_GPUColorTargetInfo colorTarget =
        Boilerplate::makeColorTargetClear(sceneColor, SDL_FColor{.r = 0.08f, .g = 0.08f, .b = 0.12f, .a = 1.0f});

    SDL_GPURenderPass* geometryPass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, &depthTarget_);
    SDL_BindGPUGraphicsPipeline(geometryPass, geometryPipeline_);

    bindLightShadowInfo(geometryPass, cmd);

    const glm::mat4 viewProjection = camera_.getViewProjectionMatrix();

    SDL_PushGPUVertexUniformData(cmd, 0, &viewProjection, sizeof(glm::mat4));
    const Uint64 gt2 = SDL_GetPerformanceCounter();
    drawWorldModelInstances(geometryPass, cmd, false);
    const Uint64 gt3 = SDL_GetPerformanceCounter();
    drawEntityModels(geometryPass, cmd, false);
    const Uint64 gt4 = SDL_GetPerformanceCounter();

    drawSkinnedModels(geometryPass, cmd);
    const Uint64 gt5 = SDL_GetPerformanceCounter();
    drawParticles(geometryPass, cmd);
    const Uint64 gt6 = SDL_GetPerformanceCounter();

    // drawWeapon(geometryPass, cmd);

    SDL_EndGPURenderPass(geometryPass);
    const Uint64 gt7 = SDL_GetPerformanceCounter();

    // TEMP attribution for the color geometry pass.
    {
        static double aUp = 0, aBegin = 0, aWorld = 0, aEnt = 0, aSkin = 0, aPart = 0, aEnd = 0;
        static int n = 0;
        aUp += ticksToMs(gt1 - gt0, gfreq);
        aBegin += ticksToMs(gt2 - gt1, gfreq);
        aWorld += ticksToMs(gt3 - gt2, gfreq);
        aEnt += ticksToMs(gt4 - gt3, gfreq);
        aSkin += ticksToMs(gt5 - gt4, gfreq);
        aPart += ticksToMs(gt6 - gt5, gfreq);
        aEnd += ticksToMs(gt7 - gt6, gfreq);
        if (++n >= 300) {
            SDL_Log("[geom] partUpload=%.3f begin+bindLight=%.3f world=%.3f entity=%.3f skinned=%.3f "
                    "particles=%.3f endPass=%.3f (cache=%zu)",
                    aUp / n, aBegin / n, aWorld / n, aEnt / n, aSkin / n, aPart / n, aEnd / n, worldBatches_.size());
            aUp = aBegin = aWorld = aEnt = aSkin = aPart = aEnd = 0;
            n = 0;
        }
    }
}

void NewRenderer::drawWeaponPass(SDL_GPUTexture* sceneColor, SDL_GPUCommandBuffer* cmd)
{
    SDL_GPUColorTargetInfo colorTarget = Boilerplate::makeColorTargetLoad(sceneColor);

    SDL_GPUDepthStencilTargetInfo depthInfo = depthTarget_; // copy
    depthInfo.load_op = SDL_GPU_LOADOP_CLEAR;               // override to clear
    depthInfo.clear_depth = 1.0f;

    SDL_GPURenderPass* geometryPass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, &depthInfo);
    SDL_BindGPUGraphicsPipeline(geometryPass, geometryPipeline_);

    const glm::mat4 viewProjection = camera_.getViewProjectionMatrix();
    SDL_PushGPUVertexUniformData(cmd, 0, &viewProjection, sizeof(glm::mat4));

    bindLightShadowInfo(geometryPass, cmd);

    drawWeapon(geometryPass, cmd);

    SDL_EndGPURenderPass(geometryPass);
}

void NewRenderer::drawFxaaPass(SDL_GPUTexture* sceneColor, SDL_GPUTexture* swapchain, SDL_GPUCommandBuffer* cmd)
{
    SDL_GPUColorTargetInfo colorTarget =
        Boilerplate::makeColorTargetClear(swapchain, SDL_FColor{.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f});

    SDL_GPURenderPass* fxaaPass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(fxaaPass, fxaaPipeline_);

    SDL_GPUTextureSamplerBinding sceneBinding = Boilerplate::makeTextureSamplerBinding(sceneColor, fxaaSampler_);
    SDL_BindGPUFragmentSamplers(fxaaPass, 0, &sceneBinding, 1);

    const glm::vec4 params{
        sceneWidth_ > 0 ? 1.0f / static_cast<float>(sceneWidth_) : 0.0f,
        sceneHeight_ > 0 ? 1.0f / static_cast<float>(sceneHeight_) : 0.0f,
        1.0f,
        0.0f,
    };
    SDL_PushGPUFragmentUniformData(cmd, 0, &params, sizeof(params));

    SDL_DrawGPUPrimitives(fxaaPass, 6, 1, 0, 0);
    SDL_EndGPURenderPass(fxaaPass);
}

void NewRenderer::drawParticles(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd) const
{
    // if (toggles.particles && particleSystem) {
    if (particleSystem_) {
        struct alignas(16) ParticleUniforms
        {
            glm::mat4 view;
            glm::mat4 proj;
            glm::vec3 camPos;
            float _p0;
            glm::vec3 camRight;
            float _p1;
            glm::vec3 camUp;
            float _p2;
        };
        ParticleUniforms pu{};
        pu.view = camera_.getViewMatrix();
        pu.proj = camera_.getProjectionMatrix();
        pu.camPos = camera_.getEye();
        pu.camRight = camera_.getRight();
        pu.camUp = camera_.getUp();
        SDL_PushGPUVertexUniformData(cmd, 0, &pu, sizeof(pu));
        particleSystem_->setScreenSize(static_cast<float>(depthWidth_), static_cast<float>(depthHeight_));
        particleSystem_->render(renderPass, cmd);
    }
}

void NewRenderer::drawWeapon(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd)
{
    if (!weapon_.visible)
        return;
    if (weapon_.modelIndex < 0 || static_cast<size_t>(weapon_.modelIndex) >= Asset::modelInstances_.size())
        return;

    Asset::ModelInstance& weaponModelInstance = Asset::modelInstances_.at(static_cast<size_t>(weapon_.modelIndex));
    ModelIdInt weaponModelId = weaponModelInstance.modelId_;

    if (!Asset::models_.contains(weaponModelId)) {
        std::cout << "invalid weapon ModelId" << std::endl;
        return;
    }

    drawModel(weaponModelId, weapon_.transform, renderPass, cmd);

    auto drawAttachment = [&](const ViewmodelAttachment& attachment) {
        if (!attachment.visible)
            return;
        if (attachment.modelIndex < 0 || static_cast<size_t>(attachment.modelIndex) >= Asset::modelInstances_.size())
            return;
        const ModelIdInt modelId = Asset::modelInstances_.at(static_cast<size_t>(attachment.modelIndex)).modelId_;
        if (!Asset::models_.contains(modelId))
            return;
        drawModel(modelId, attachment.transform, renderPass, cmd);
    };
    drawAttachment(weapon_.hands.right);
    drawAttachment(weapon_.hands.left);
    drawAttachment(weapon_.debugPoint);
}

void NewRenderer::drawSkinnedModels(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd)
{
    skinnedRenderer_.draw(renderPass, cmd);
}

void NewRenderer::rebuildWorldBatches(SDL_GPUCommandBuffer* cmd)
{
    // Release any GPU buffers from a prior build (SDL defers the actual free
    // until pending GPU work referencing them completes, so this is safe).
    for (auto& b : worldBatches_) {
        if (b.vbuf)
            SDL_ReleaseGPUBuffer(device_, b.vbuf);
        if (b.ibuf)
            SDL_ReleaseGPUBuffer(device_, b.ibuf);
    }
    worldBatches_.clear();

    // CPU-side accumulation: one entry per distinct material signature.
    struct BuildBatch
    {
        SDL_GPUTexture* texture = nullptr;
        SDL_GPUTexture* normalTexture = nullptr;
        SDL_GPUTexture* metallicRoughnessTexture = nullptr;
        glm::vec4 materialDiffuse{0.8f, 0.8f, 0.8f, 1.0f};
        Uint32 materialFlags[4] = {1, 0, 0, 0};
        std::vector<Asset::Vertex> verts;
        std::vector<Uint32> indices;
    };
    std::vector<BuildBatch> builds;

    for (const auto& mInstance : Asset::modelInstances_) {
        if (!mInstance.drawInScenePass)
            continue;
        const auto modelIt = Asset::models_.find(mInstance.modelId_);
        if (modelIt == Asset::models_.end())
            continue;
        const Asset::Model& model = modelIt->second;
        for (const auto& element : model.modelElements_) {
            const auto meshIt = Asset::meshes_.find(element.meshId_);
            if (meshIt == Asset::meshes_.end())
                continue;
            const Asset::Mesh& mesh = meshIt->second;
            if (mesh.vertexData_.empty() || mesh.indexData_.empty())
                continue;

            const Asset::Material* material = nullptr;
            const auto matIt = Asset::materials_.find(element.materialId_);
            if (matIt != Asset::materials_.end())
                material = &matIt->second;

            SDL_GPUTexture* texture = nullptr;
            SDL_GPUTexture* normalTexture = nullptr;
            SDL_GPUTexture* metallicRoughnessTexture = nullptr;
            if (material != nullptr) {
                const auto texIt = Asset::textures_.find(material->texId_[0]);
                if (texIt != Asset::textures_.end())
                    texture = texIt->second.tex;
                const auto nrmIt = Asset::textures_.find(material->normalTexture);
                if (nrmIt != Asset::textures_.end())
                    normalTexture = nrmIt->second.tex;
                const auto mrIt = Asset::textures_.find(material->metallicRoughnessTexture);
                if (mrIt != Asset::textures_.end())
                    metallicRoughnessTexture = mrIt->second.tex;
            }

            const bool useTexture = texture != nullptr || material == nullptr || !material->hasPhongData_;
            if (texture == nullptr)
                texture = texture_;
            if (normalTexture == nullptr)
                normalTexture = texture_;
            if (metallicRoughnessTexture == nullptr)
                metallicRoughnessTexture = texture_;

            const glm::vec4 diffuse =
                material != nullptr ? glm::vec4(material->kDiffuse_, 1.0f) : glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
            const Uint32 flags[4] = {useTexture ? 1u : 0u, normalTexture != texture_ ? 1u : 0u,
                                     metallicRoughnessTexture != texture_ ? 1u : 0u, 0u};

            // Find or create the matching material batch (linear scan — there
            // are only a handful of distinct materials).
            BuildBatch* batch = nullptr;
            for (auto& b : builds) {
                if (b.texture == texture && b.normalTexture == normalTexture &&
                    b.metallicRoughnessTexture == metallicRoughnessTexture && b.materialDiffuse == diffuse &&
                    b.materialFlags[0] == flags[0] && b.materialFlags[1] == flags[1] &&
                    b.materialFlags[2] == flags[2]) {
                    batch = &b;
                    break;
                }
            }
            if (batch == nullptr) {
                BuildBatch nb{};
                nb.texture = texture;
                nb.normalTexture = normalTexture;
                nb.metallicRoughnessTexture = metallicRoughnessTexture;
                nb.materialDiffuse = diffuse;
                nb.materialFlags[0] = flags[0];
                nb.materialFlags[1] = flags[1];
                nb.materialFlags[2] = flags[2];
                nb.materialFlags[3] = 0u;
                builds.push_back(std::move(nb));
                batch = &builds.back();
            }

            // Bake this element's vertices into world space so the shader's
            // per-object model matrix can stay the identity.
            const glm::mat4 model_m = mInstance.transform_ * element.cachedTransform_;
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model_m)));
            const glm::mat3 rot = glm::mat3(model_m);
            const auto base = static_cast<Uint32>(batch->verts.size());
            batch->verts.reserve(batch->verts.size() + mesh.vertexData_.size());
            for (const Asset::Vertex& sv : mesh.vertexData_) {
                Asset::Vertex wv;
                wv.position = glm::vec3(model_m * glm::vec4(sv.position, 1.0f));
                wv.normal = glm::normalize(normalMatrix * sv.normal);
                wv.texUV = sv.texUV;
                wv.tangent = glm::vec4(glm::normalize(rot * glm::vec3(sv.tangent)), sv.tangent.w);
                batch->verts.push_back(wv);
            }
            batch->indices.reserve(batch->indices.size() + mesh.indexData_.size());
            for (const Uint32 idx : mesh.indexData_)
                batch->indices.push_back(base + idx);
        }
    }

    // Create GPU buffers and queue one combined upload.
    std::vector<Boilerplate::BufferUpload> uploads;
    uploads.reserve(builds.size() * 2);
    for (auto& b : builds) {
        if (b.indices.empty())
            continue;
        const size_t vsize = b.verts.size() * sizeof(Asset::Vertex);
        const size_t isize = b.indices.size() * sizeof(Uint32);

        WorldBatch wb{};
        wb.vbuf = Boilerplate::createBuffer(device_, vsize, SDL_GPU_BUFFERUSAGE_VERTEX);
        wb.ibuf = Boilerplate::createBuffer(device_, isize, SDL_GPU_BUFFERUSAGE_INDEX);
        wb.indexCount = static_cast<Uint32>(b.indices.size());
        wb.texture = b.texture;
        wb.normalTexture = b.normalTexture;
        wb.metallicRoughnessTexture = b.metallicRoughnessTexture;
        wb.materialDiffuse = b.materialDiffuse;
        wb.materialFlags[0] = b.materialFlags[0];
        wb.materialFlags[1] = b.materialFlags[1];
        wb.materialFlags[2] = b.materialFlags[2];
        wb.materialFlags[3] = 0u;

        uploads.push_back({wb.vbuf, b.verts.data(), static_cast<Uint32>(vsize)});
        uploads.push_back({wb.ibuf, b.indices.data(), static_cast<Uint32>(isize)});
        worldBatches_.push_back(wb);
    }

    Boilerplate::uploadBuffers(device_, cmd, uploads);
    worldBatchesDirty_ = false;
}

void NewRenderer::drawWorldModelInstances(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd, bool depth)
{
    // The shadow depth pass (depth=true) runs at most ~30 Hz, so leave it on the
    // simple per-instance path.  The per-frame color pass (depth=false) replays
    // the pre-baked, material-merged batches: one draw per material, identity
    // model matrix (vertices are already in world space).
    if (depth) {
        for (const auto& mInstance : Asset::modelInstances_) {
            if (!mInstance.drawInScenePass)
                continue;
            drawModelDepth(mInstance.modelId_, mInstance.transform_, renderPass, cmd);
        }
        return;
    }

    static const glm::mat4 kIdentity(1.0f);
    SDL_PushGPUVertexUniformData(cmd, 1, &kIdentity, sizeof(glm::mat4));

    SDL_GPUTexture* boundTex = nullptr;
    SDL_GPUTexture* boundNrm = nullptr;
    SDL_GPUTexture* boundMr = nullptr;
    for (const auto& b : worldBatches_) {
        if (b.texture != boundTex) {
            SDL_GPUTextureSamplerBinding textureBinding = Boilerplate::makeTextureSamplerBinding(b.texture, sampler_);
            SDL_BindGPUFragmentSamplers(renderPass, 0, &textureBinding, 1);
            boundTex = b.texture;
        }
        if (b.normalTexture != boundNrm || b.metallicRoughnessTexture != boundMr) {
            SDL_GPUTextureSamplerBinding pbrTextureBindings[] = {
                Boilerplate::makeTextureSamplerBinding(b.normalTexture, sampler_),
                Boilerplate::makeTextureSamplerBinding(b.metallicRoughnessTexture, sampler_),
            };
            SDL_BindGPUFragmentSamplers(renderPass, 2, pbrTextureBindings, 2);
            boundNrm = b.normalTexture;
            boundMr = b.metallicRoughnessTexture;
        }

        SDL_PushGPUFragmentUniformData(cmd, 0, &b.materialDiffuse, sizeof(b.materialDiffuse));
        SDL_PushGPUFragmentUniformData(cmd, 1, &b.materialFlags, sizeof(b.materialFlags));

        SDL_GPUBufferBinding vertexBufferBinding{};
        vertexBufferBinding.buffer = b.vbuf;
        vertexBufferBinding.offset = 0;
        SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);

        SDL_GPUBufferBinding indexBufferBinding{};
        indexBufferBinding.buffer = b.ibuf;
        indexBufferBinding.offset = 0;
        SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_DrawGPUIndexedPrimitives(renderPass, b.indexCount, 1, 0, 0, 0);
    }
}

void NewRenderer::drawEntityModels(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd, bool depth)
{
    for (const auto& entityCmd : entities_) {
        if (entityCmd.modelIndex < 0) {
            std::cout << "invalid modelIndex" << std::endl;
            continue;
        }
        if (static_cast<size_t>(entityCmd.modelIndex) >= Asset::modelInstances_.size())
            continue;
        ModelIdInt modelId = Asset::modelInstances_.at(static_cast<size_t>(entityCmd.modelIndex)).modelId_;
        // TODO(graphics): pass entityCmd.tint into the per-mesh material UBO
        // so tinted entities (e.g. team colors, hit flashes) render correctly.

        if (depth) {
            drawModelDepth(modelId, entityCmd.worldTransform, renderPass, cmd);
        } else {
            drawModel(modelId, entityCmd.worldTransform, renderPass, cmd);
        }
    }
}

void NewRenderer::drawModel(ModelIdInt modelId,
                            const glm::mat4& modelTransform,
                            SDL_GPURenderPass* renderPass,
                            SDL_GPUCommandBuffer* cmd)
{
    Asset::Model& model = Asset::models_.at(modelId);
    for (auto& element : model.modelElements_) {
        const Asset::Material* material = nullptr;
        if (Asset::materials_.contains(element.materialId_))
            material = &Asset::materials_.at(element.materialId_);

        SDL_GPUTexture* texture = nullptr;
        SDL_GPUTexture* normalTexture = nullptr;
        SDL_GPUTexture* metallicRoughnessTexture = nullptr;
        if (material != nullptr) {
            const TexIdInt texId = material->texId_[0];
            if (Asset::textures_.contains(texId))
                texture = Asset::textures_.at(texId).tex;
            if (Asset::textures_.contains(material->normalTexture))
                normalTexture = Asset::textures_.at(material->normalTexture).tex;
            if (Asset::textures_.contains(material->metallicRoughnessTexture))
                metallicRoughnessTexture = Asset::textures_.at(material->metallicRoughnessTexture).tex;
        }

        const bool useTexture = texture != nullptr || material == nullptr || !material->hasPhongData_;
        if (texture == nullptr)
            texture = texture_;
        if (normalTexture == nullptr)
            normalTexture = texture_;
        if (metallicRoughnessTexture == nullptr)
            metallicRoughnessTexture = texture_;

        SDL_GPUTextureSamplerBinding textureBinding = Boilerplate::makeTextureSamplerBinding(texture, sampler_);
        SDL_BindGPUFragmentSamplers(renderPass, 0, &textureBinding, 1);

        SDL_GPUTextureSamplerBinding pbrTextureBindings[] = {
            Boilerplate::makeTextureSamplerBinding(normalTexture, sampler_),
            Boilerplate::makeTextureSamplerBinding(metallicRoughnessTexture, sampler_),
        };
        SDL_BindGPUFragmentSamplers(renderPass, 2, pbrTextureBindings, 2);

        glm::vec4 materialDiffuse{0.8f, 0.8f, 0.8f, 1.0f};
        if (material != nullptr)
            materialDiffuse = glm::vec4(material->kDiffuse_, 1.0f);
        struct MaterialFlags
        {
            Uint32 useTexture;
            Uint32 useNormalTexture;
            Uint32 useMetallicRoughnessTexture;
            Uint32 _pad0;
        } materialFlags{
            useTexture ? 1u : 0u,
            normalTexture != texture_ ? 1u : 0u,
            metallicRoughnessTexture != texture_ ? 1u : 0u,
            0u,
        };
        SDL_PushGPUFragmentUniformData(cmd, 0, &materialDiffuse, sizeof(materialDiffuse));
        SDL_PushGPUFragmentUniformData(cmd, 1, &materialFlags, sizeof(materialFlags));

        glm::mat4 modelElementMatrix = modelTransform * element.cachedTransform_;
        SDL_PushGPUVertexUniformData(cmd, 1, &modelElementMatrix, sizeof(glm::mat4));

        Asset::Mesh& mesh = Asset::meshes_.at(element.meshId_);
        drawMesh(renderPass, mesh);
    }
}

void NewRenderer::drawModelDepth(ModelIdInt modelId,
                                 const glm::mat4& modelTransform,
                                 SDL_GPURenderPass* renderPass,
                                 SDL_GPUCommandBuffer* cmd)
{
    Asset::Model& model = Asset::models_.at(modelId);
    for (auto& element : model.modelElements_) {
        // SDL_Log("drawModelDepth: modelId=%d elements=%zu", modelId, model.modelElements_.size());
        glm::mat4 modelElementMatrix = modelTransform * element.cachedTransform_;
        SDL_PushGPUVertexUniformData(cmd, 1, &modelElementMatrix, sizeof(glm::mat4));
        Asset::Mesh& mesh = Asset::meshes_.at(element.meshId_);
        // SDL_Log("  vbuff=%p ibuff=%p indexCount=%u",
        //     (void*)mesh.vBufferInfo_.gpuBuff,
        //     (void*)mesh.iBufferInfo_.gpuBuff,
        //     mesh.iBufferInfo_.bufferSize / (uint32_t)sizeof(Uint32));
        drawMesh(renderPass, mesh);
    }
}

void NewRenderer::drawMesh(SDL_GPURenderPass* renderPass, const Asset::Mesh& mesh) const
{
    SDL_GPUBufferBinding vertexBufferBinding{};
    vertexBufferBinding.buffer = mesh.vBufferInfo_.gpuBuff;
    vertexBufferBinding.offset = 0;
    SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);

    SDL_GPUBufferBinding indexBufferBinding{};
    indexBufferBinding.buffer = mesh.iBufferInfo_.gpuBuff;
    indexBufferBinding.offset = 0;
    SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    const Uint32 indexCount = static_cast<Uint32>(mesh.iBufferInfo_.bufferSize / sizeof(Uint32));
    SDL_DrawGPUIndexedPrimitives(renderPass, indexCount, 1, 0, 0, 0);
}

bool NewRenderer::ensureDepthTextureSize(Uint32 width, Uint32 height)
{
    if (depthTarget_.texture && depthWidth_ == width && depthHeight_ == height)
        return true;

    if (depthTarget_.texture) {
        SDL_ReleaseGPUTexture(device_, depthTarget_.texture);
        depthTarget_.texture = nullptr;
    }

    depthTarget_ = Boilerplate::makeDepthTarget(Boilerplate::createDepthTexture(device_, width, height), 0, false);

    if (!depthTarget_.texture)
        return false;

    depthWidth_ = width;
    depthHeight_ = height;
    return true;
}

bool NewRenderer::ensureSceneTextureSize(Uint32 width, Uint32 height)
{
    if (sceneColor_ && sceneWidth_ == width && sceneHeight_ == height)
        return true;

    if (sceneColor_) {
        SDL_ReleaseGPUTexture(device_, sceneColor_);
        sceneColor_ = nullptr;
    }

    sceneColor_ = Boilerplate::createSampledColorTarget(device_, width, height, colorTarget_);
    if (!sceneColor_)
        return false;

    sceneWidth_ = width;
    sceneHeight_ = height;
    return true;
}

void NewRenderer::drawUIPass(SDL_GPUTexture* swapchain, SDL_GPUCommandBuffer* cmd)
{
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData)
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, cmd);

    SDL_GPUColorTargetInfo uiColorTarget = Boilerplate::makeColorTargetLoad(swapchain);

    SDL_GPURenderPass* uiPass = SDL_BeginGPURenderPass(cmd, &uiColorTarget, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(uiPass, hudPipeline_);

    if (drawData && imguiEnabled)
        ImGui_ImplSDLGPU3_RenderDrawData(drawData, cmd, uiPass);

    SDL_EndGPURenderPass(uiPass);
}

void NewRenderer::drawHudPass(SDL_GPUTexture* target, SDL_GPUCommandBuffer* cmd)
{
    if (hudTexture_ == nullptr)
        return;

    SDL_GPUColorTargetInfo hudColorTarget = Boilerplate::makeColorTargetLoad(target);

    SDL_GPURenderPass* hudPass = SDL_BeginGPURenderPass(cmd, &hudColorTarget, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(hudPass, hudPipeline_);
    drawHud(hudPass);
    SDL_EndGPURenderPass(hudPass);
}

void NewRenderer::drawHud(SDL_GPURenderPass* renderPass)
{
    SDL_GPUTextureSamplerBinding hudTextureBinding = Boilerplate::makeTextureSamplerBinding(hudTexture_, hudSampler_);
    SDL_BindGPUFragmentSamplers(renderPass, 0, &hudTextureBinding, 1);

    SDL_DrawGPUPrimitives(renderPass, 6, 1, 0, 0);
}

void NewRenderer::setHudTexture(SDL_GPUTexture* hudTexture)
{
    hudTexture_ = hudTexture;
}

void NewRenderer::quit()
{
    if (device_) {
        SDL_WaitForGPUIdle(device_);

        if (depthTarget_.texture)
            SDL_ReleaseGPUTexture(device_, depthTarget_.texture);
        if (sceneColor_)
            SDL_ReleaseGPUTexture(device_, sceneColor_);

        for (auto& meshPair : Asset::meshes_) {
            Asset::Mesh& mesh = meshPair.second;

            if (mesh.vBufferInfo_.gpuBuff)
                SDL_ReleaseGPUBuffer(device_, mesh.vBufferInfo_.gpuBuff);
            if (mesh.iBufferInfo_.gpuBuff)
                SDL_ReleaseGPUBuffer(device_, mesh.iBufferInfo_.gpuBuff);

            mesh.vBufferInfo_ = {};
            mesh.iBufferInfo_ = {};
        }

        skinnedRenderer_.shutdown();

        if (geometryPipeline_)
            SDL_ReleaseGPUGraphicsPipeline(device_, geometryPipeline_);
        if (hudPipeline_)
            SDL_ReleaseGPUGraphicsPipeline(device_, hudPipeline_);
        if (fxaaPipeline_)
            SDL_ReleaseGPUGraphicsPipeline(device_, fxaaPipeline_);
        if (sampler_)
            SDL_ReleaseGPUSampler(device_, sampler_);
        if (hudSampler_)
            SDL_ReleaseGPUSampler(device_, hudSampler_);
        if (fxaaSampler_)
            SDL_ReleaseGPUSampler(device_, fxaaSampler_);
        if (texture_)
            SDL_ReleaseGPUTexture(device_, texture_);

        ImGui_ImplSDLGPU3_Shutdown();
        SDL_ReleaseWindowFromGPUDevice(device_, window_);
        SDL_DestroyGPUDevice(device_);
    }

    window_ = nullptr;
    device_ = nullptr;
    shaderFormat_ = SDL_GPU_SHADERFORMAT_INVALID;

    geometryPipeline_ = nullptr;
    hudPipeline_ = nullptr;
    fxaaPipeline_ = nullptr;
    depthTarget_.texture = nullptr;
    sceneColor_ = nullptr;
    texture_ = nullptr;
    sampler_ = nullptr;
    hudTexture_ = nullptr;
    hudSampler_ = nullptr;
    fxaaSampler_ = nullptr;
    sceneWidth_ = 0;
    sceneHeight_ = 0;
    depthWidth_ = 0;
    depthHeight_ = 0;
}

// ─── Static models ──────────────────────────────────────────────────────────

int NewRenderer::loadSceneModel(
    const char* filename, glm::vec3 pos, float scale, bool flipUVs, const std::string& /*excludeNodesContaining*/)
{
    ModelIdInt modelId = Asset::getIdFromString(filename);
    const bool flatten = false;
    const std::vector<std::string> texFileNames;

    const char* const base = SDL_GetBasePath();
    std::filesystem::path fullPath = base ? base : "";
    fullPath /= ASSETS_DIR;
    fullPath /= filename;

    if (!AssetLoader::loadModel(modelId, fullPath.string(), texFileNames, flatten, flipUVs)) {
        SDL_Log("NewRenderer::loadSceneModel: AssetLoader::loadModel('%s') failed", fullPath.string().c_str());
        Asset::models_.erase(modelId);
        return -1;
    }
    Asset::Model& model = Asset::models_.at(modelId);
    AssetLoader::updateModelTransformCache(modelId);

    auto modelTransform = glm::mat4(1.0f);
    modelTransform = glm::scale(modelTransform, glm::vec3(scale));
    modelTransform[3] = glm::vec4(pos, 1.0f);

    Asset::ModelInstance sceneInstance{};
    sceneInstance.drawInScenePass = true;
    sceneInstance.modelId_ = modelId;
    sceneInstance.transform_ = modelTransform;

    Asset::modelInstances_.push_back(sceneInstance);
    worldBatchesDirty_ = true;

    std::vector<Boilerplate::BufferUpload> uploads;
    auto uploadTexture = [&](TexIdInt texId, SDL_GPUTextureFormat format) {
        if (texId == 0 || !Asset::textures_.contains(texId))
            return;

        Asset::Texture& tex = Asset::textures_.at(texId);
        if (tex.tex == nullptr && tex.tex_raw != nullptr && tex.width > 0 && tex.height > 0) {
            tex.tex = Boilerplate::createTextureRGBA8(
                device_, static_cast<Uint32>(tex.width), static_cast<Uint32>(tex.height), tex.tex_raw, format);
            stbi_image_free(tex.tex_raw);
            tex.tex_raw = nullptr;
        }
    };

    for (auto& element : model.modelElements_) {
        createMeshBuffers(element.meshId_);
        Asset::Mesh& mesh = Asset::meshes_[element.meshId_];
        uploads.push_back({mesh.vBufferInfo_.gpuBuff, mesh.vBufferInfo_.srcData, mesh.vBufferInfo_.bufferSize});
        uploads.push_back({mesh.iBufferInfo_.gpuBuff, mesh.iBufferInfo_.srcData, mesh.iBufferInfo_.bufferSize});

        MaterialIdInt matId = element.materialId_;
        if (!Asset::materials_.contains(matId))
            continue;

        Asset::Material& mat = Asset::materials_.at(matId);
        uploadTexture(mat.texId_[0], SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
        uploadTexture(mat.normalTexture, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        uploadTexture(mat.metallicRoughnessTexture, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) {
        SDL_Log("NewRenderer: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return -1;
    }

    Boilerplate::uploadBuffers(device_, cmd, uploads);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(device_);

    return static_cast<int>(Asset::modelInstances_.size() - 1);
}

int NewRenderer::modelCount() const
{
    return static_cast<int>(Asset::models_.size());
}

// ─── Per-frame data-capture stubs ────────────────────────────────────────────

void NewRenderer::setWeaponViewmodel(const WeaponViewmodel& vm)
{
    weapon_ = vm;
}

void NewRenderer::setPointLights(std::vector<PointLight> pointLights)
{
    // TODO(graphics): consume `pointLights_` inside the geometry / skinned
    // passes by pushing a light-array UBO to the fragment shader.  Cap at the
    // shader's array size and silently drop the rest.
    // pointLights_ = std::move(pointLights);
    sceneLightInfo_.numPointLights =
        std::min(static_cast<uint32_t>(pointLights.size()), static_cast<uint32_t>(MAX_POINT_LIGHTS));

    memcpy(sceneLightInfo_.pointLights, pointLights.data(), sceneLightInfo_.numPointLights * sizeof(PointLight));
}

void NewRenderer::setEntityRenderList(std::vector<EntityRenderCmd>&& entityList)
{
    entities_ = std::move(entityList);
}

void NewRenderer::setModelEmissive(int32_t modelIdUnsanitized, glm::vec4 emissiveColor)
{
    // TODO(graphics): read this side-table inside `drawModel` when composing
    // the material UBO.  Emissive should add (not multiply) into the lit
    // colour so glowing bodies (beam cylinders, sphere lights) still pop in
    // dark areas.
    g_emissiveOverrides[modelIdUnsanitized] = emissiveColor;
}

void NewRenderer::setModelScenePass(int32_t modelIndex, bool drawInScene)
{
    if (modelIndex < 0 || static_cast<size_t>(modelIndex) >= Asset::modelInstances_.size())
        return;
    Asset::modelInstances_.at(static_cast<size_t>(modelIndex)).drawInScenePass = drawInScene;
    worldBatchesDirty_ = true;
}

void NewRenderer::setParticleSystem(ParticleSystem* ps)
{
    // TODO(graphics): inside `drawFrame`, before BeginRenderPass call
    //   particleSystem_->uploadToGpu(cmd);
    // and inside the main HDR pass call
    //   particleSystem_->render(pass, cmd);
    // See ParticleSystem.hpp doc-comment for the lifecycle (init/update/quit).
    particleSystem_ = ps;
}

bool NewRenderer::setVSync(bool enabled)
{
    // VSYNC (FIFO) makes SDL_AcquireGPUSwapchainTexture block until the next
    // refresh interval — at high frame rates that block dominates the frame
    // (measured ~2.8 ms / frame).  When vsync is requested off we switch the
    // swapchain to IMMEDIATE (uncapped, may tear) so acquire returns as soon
    // as an image is free, falling back to MAILBOX then VSYNC if the platform
    // doesn't support it.
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!enabled) {
        if (SDL_WindowSupportsGPUPresentMode(device_, window_, SDL_GPU_PRESENTMODE_IMMEDIATE))
            mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
        else if (SDL_WindowSupportsGPUPresentMode(device_, window_, SDL_GPU_PRESENTMODE_MAILBOX))
            mode = SDL_GPU_PRESENTMODE_MAILBOX;
    }

    if (!SDL_SetGPUSwapchainParameters(device_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
        SDL_Log("NewRenderer::setVSync: SDL_SetGPUSwapchainParameters failed: %s", SDL_GetError());
        return false;
    }
    vsyncEnabled_ = enabled;
    SDL_Log("NewRenderer::setVSync: present mode = %s",
            mode == SDL_GPU_PRESENTMODE_VSYNC       ? "VSYNC"
            : mode == SDL_GPU_PRESENTMODE_IMMEDIATE ? "IMMEDIATE"
                                                    : "MAILBOX");
    return true;
}

void NewRenderer::requestScreenshot(const std::string& path)
{
    // TODO(graphics): after `SubmitGPUCommandBuffer` in `drawFrame`, copy the
    // swapchain texture into a download transfer buffer, map CPU-side, write
    // `path` as a PNG via `stbi_write_png` (4 channels, R8G8B8A8).  Clear
    // `pendingScreenshotPath_` after writing.
    pendingScreenshotPath_ = path;
}

void NewRenderer::updateModelMeshVertices(int /*modelIndex*/,
                                          int /*meshIndex*/,
                                          const Vertex* /*vertices*/,
                                          Uint32 /*vertexCount*/)
{
    // TODO(graphics): legacy used this for CPU-skinning (now superseded by
    // setSkinnedFrame).  Leave as a no-op unless a new caller emerges.
}

bool NewRenderer::loadHDRSkybox(const std::string& /*path*/)
{
    // TODO(graphics): load via stb_image float, upload as a 2D HDR texture,
    // equirect→cubemap convolution, derive irradiance + prefilter mips.
    // Set `useHDRSkybox = true` and `currentHDRName = stem-of(path)` on success.
    return false;
}

void NewRenderer::scanHDRFiles()
{
    // TODO(graphics): iterate `assets/hdr/*.hdr` via std::filesystem and fill
    // `availableHDRFiles` with absolute paths.  Called once at init.
    availableHDRFiles.clear();
}

bool NewRenderer::setRig(const std::vector<RigMeshSource>& meshes, int numJoints)
{
    return skinnedRenderer_.setRig(meshes, numJoints);
}

void NewRenderer::setSkinnedFrame(const std::vector<glm::mat4>& palette, const std::vector<SkinnedInstance>& instances)
{
    skinnedRenderer_.setFrame(palette, instances);
}
