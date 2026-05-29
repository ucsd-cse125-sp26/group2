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
    if (!SDL_AcquireGPUSwapchainTexture(cmd, window_, &swapchain, &width, &height)) {
        SDL_Log("NewRenderer::drawFrame: SDL_AcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

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
    {
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        if (copyPass) {
            skinnedRenderer_.uploadFrame(cmd, copyPass);
            SDL_EndGPUCopyPass(copyPass);
        }
    }

    ///////////////////////////////////// SHADOWMAP CREATION /////////////////////////////////////////////
    // constexpr uint32_t shadowSize = 2048;
    //  constexpr uint32_t shadowSize = 1024;
    constexpr uint32_t shadowSize = 1024;
    constexpr Uint8 numCubeFaces = 6;

    static glm::vec3 cubeFaceTargets[numCubeFaces];
    static glm::vec3 cubeFaceUps[numCubeFaces];

    cubeFaceTargets[0] = glm::vec3(1, 0, 0);
    cubeFaceTargets[1] = glm::vec3(-1, 0, 0);
    cubeFaceTargets[2] = glm::vec3(0, 1, 0);
    cubeFaceTargets[3] = glm::vec3(0, -1, 0);
    cubeFaceTargets[4] = glm::vec3(0, 0, 1);
    cubeFaceTargets[5] = glm::vec3(0, 0, -1);

    cubeFaceUps[0] = glm::vec3(0, -1, 0);
    cubeFaceUps[1] = glm::vec3(0, -1, 0);
    cubeFaceUps[2] = glm::vec3(0, 0, 1);
    cubeFaceUps[3] = glm::vec3(0, 0, -1);
    cubeFaceUps[4] = glm::vec3(0, -1, 0);
    cubeFaceUps[5] = glm::vec3(0, -1, 0);

    std::vector<PointLight> sampleLights;
    PointLight pl0{};
    pl0.position = glm::vec3(300, 100.0f, 500);
    pl0.intensity = 250000;
    pl0.color = glm::vec3(1.0f, 0.7f, 0.5f);
    pl0.range = 500.0f;
    sampleLights.push_back(pl0);
    setPointLights(sampleLights);

    SDL_GPUTexture* shadowMap = nullptr;

    // SDL_Log("farPlane=%f numLights=%u",
    //     sceneLightInfo_.pointLightFarPlane,
    //     sceneLightInfo_.numPointLights);

    const Uint64 tShadowAlloc0 = SDL_GetPerformanceCounter();
    if (sceneLightInfo_.numPointLights < 1) {
        shadowMap = Boilerplate::createEmptyTextureD32F(device_, 1, 1, true, MAX_POINT_LIGHTS);
    } else {
        // std::cout << "NewRenderer::drawFrame: sceneLightInfo_.numPointLights = " << sceneLightInfo_.numPointLights <<
        // std::endl;
        shadowMap = Boilerplate::createEmptyTextureD32F(device_, shadowSize, shadowSize, true, MAX_POINT_LIGHTS);
    }
    const Uint64 tShadowAlloc1 = SDL_GetPerformanceCounter();
    if (sceneLightInfo_.numPointLights >= 1) {

        glm::mat4 shadowProjection = glm::perspective(
            glm::radians(90.0f), 1.0f, sceneLightInfo_.pointLightNearPlane, sceneLightInfo_.pointLightFarPlane);
        shadowProjection[1][1] *= -1;
        for (Uint8 iLight = 0; iLight < sceneLightInfo_.numPointLights; iLight++) {
            PointLight& light = sceneLightInfo_.pointLights[iLight];
            glm::vec3 yNegatedLightPosition = light.position;
            // yNegatedLightPosition.y *= -1.0f;

            for (int face = 0; face < numCubeFaces; face++) {
                glm::vec3& iCubeFaceTarget = cubeFaceTargets[face];
                glm::vec3& iCubeFaceUp = cubeFaceUps[face];

                glm::mat4 shadowView =
                    glm::lookAt(yNegatedLightPosition, yNegatedLightPosition + iCubeFaceTarget, iCubeFaceUp);
                const glm::mat4 shadowViewProjection = shadowProjection * shadowView;

                drawGeometryDepthPass(shadowMap, iLight * 6 + face, cmd, shadowViewProjection);
            }
        }
    }

    const Uint64 tShadowPasses1 = SDL_GetPerformanceCounter();

    ///////////////////////////////////// SHADOWMAP CREATION /////////////////////////////////////////////
    // shadowMapBindings_.push({shadowMap, depthSampler_});
    shadowMapTextureDeletionQueue.push(shadowMap);

    float fov = 60.0f;
    setMainCamera(eye, yaw, pitch, roll, width, height, fov);

    const Uint64 tGeom0 = SDL_GetPerformanceCounter();
    drawGeometryPass(sceneColor_, cmd);
    const Uint64 tGeom1 = SDL_GetPerformanceCounter();
    drawWeaponPass(sceneColor_, cmd);
    drawFxaaPass(sceneColor_, swapchain, cmd);
    drawHudPass(swapchain, cmd);
    drawUIPass(swapchain, cmd);

    const Uint64 t2 = SDL_GetPerformanceCounter();
    lastRecordMs_ = ticksToMs(t2 - t1, freq);

    // TEMP attribution: accumulate sub-pass costs and log averages periodically.
    {
        static double accShadowAlloc = 0.0, accShadowPasses = 0.0, accGeom = 0.0, accRest = 0.0, accRec = 0.0;
        static int accN = 0;
        const double msShadowAlloc = ticksToMs(tShadowAlloc1 - tShadowAlloc0, freq);
        const double msShadowPasses = ticksToMs(tShadowPasses1 - tShadowAlloc1, freq);
        const double msGeom = ticksToMs(tGeom1 - tGeom0, freq);
        const double msRest = ticksToMs(t2 - tGeom1, freq);
        accShadowAlloc += msShadowAlloc;
        accShadowPasses += msShadowPasses;
        accGeom += msGeom;
        accRest += msRest;
        accRec += lastRecordMs_;
        if (++accN >= 300) {
            SDL_Log("[attr] rec=%.3f | shadowAlloc=%.3f shadowPasses=%.3f geometry=%.3f rest(weapon+fxaa+hud+ui)=%.3f",
                    accRec / accN, accShadowAlloc / accN, accShadowPasses / accN, accGeom / accN, accRest / accN);
            accShadowAlloc = accShadowPasses = accGeom = accRest = accRec = 0.0;
            accN = 0;
        }
    }

    SDL_SubmitGPUCommandBuffer(cmd);

    const Uint64 t3 = SDL_GetPerformanceCounter();
    lastSubmitMs_ = ticksToMs(t3 - t2, freq);

    // for (auto smb : shadowMapBindings_) {
    // for (auto smb : shadowMapBindings_) {
    if (shadowMapTextureDeletionQueue.size() > 1) {
        SDL_ReleaseGPUTexture(device_, shadowMapTextureDeletionQueue.front());
        shadowMapTextureDeletionQueue.pop();
    }

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
    SDL_GPUTextureSamplerBinding shadowMapBinding{shadowMapTextureDeletionQueue.back(), depthSampler_};
    SDL_BindGPUFragmentSamplers(renderPass, 1, &shadowMapBinding, 1);

    SDL_PushGPUFragmentUniformData(cmd, 2, &sceneLightInfo_, sizeof(LightUBO));
}
void NewRenderer::drawGeometryPass(SDL_GPUTexture* sceneColor, SDL_GPUCommandBuffer* cmd)
{
    if (particleSystem_)
        particleSystem_->uploadToGpu(cmd); // Must be before render pass

    SDL_GPUColorTargetInfo colorTarget =
        Boilerplate::makeColorTargetClear(sceneColor, SDL_FColor{.r = 0.08f, .g = 0.08f, .b = 0.12f, .a = 1.0f});

    SDL_GPURenderPass* geometryPass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, &depthTarget_);
    SDL_BindGPUGraphicsPipeline(geometryPass, geometryPipeline_);

    bindLightShadowInfo(geometryPass, cmd);

    const glm::mat4 viewProjection = camera_.getViewProjectionMatrix();

    SDL_PushGPUVertexUniformData(cmd, 0, &viewProjection, sizeof(glm::mat4));
    drawWorldModelInstances(geometryPass, cmd, false);
    drawEntityModels(geometryPass, cmd, false);

    drawSkinnedModels(geometryPass, cmd);
    drawParticles(geometryPass, cmd);

    // drawWeapon(geometryPass, cmd);

    SDL_EndGPURenderPass(geometryPass);
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

void NewRenderer::drawWorldModelInstances(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd, bool depth)
{
    for (const auto& mInstance : Asset::modelInstances_) {
        if (!mInstance.drawInScenePass)
            continue;
        if (depth) {
            drawModelDepth(mInstance.modelId_, mInstance.transform_, renderPass, cmd);
        } else {
            drawModel(mInstance.modelId_, mInstance.transform_, renderPass, cmd);
        }
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
    // TODO(graphics): apply via SDL_SetGPUSwapchainParameters with
    //   SDL_GPU_PRESENTMODE_VSYNC (or MAILBOX) when enabled, and
    //   SDL_GPU_PRESENTMODE_IMMEDIATE when disabled.  Check the format
    //   you currently use for the swapchain so you preserve it.
    vsyncEnabled_ = enabled;
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
