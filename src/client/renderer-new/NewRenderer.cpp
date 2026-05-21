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

#include <backends/imgui_impl_sdlgpu3.h>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/trigonometric.hpp>
#include <imgui.h>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace
{
/// Convert SDL high-resolution counter ticks to milliseconds.
float ticksToMs(Uint64 elapsed, Uint64 freq)
{
    return freq == 0 ? 0.0f : (static_cast<float>(elapsed) * 1000.0f) / static_cast<float>(freq);
}

class ScopedRendererTimer
{
public:
    ScopedRendererTimer(float& outMs, Uint64 freq) : outMs_(outMs), freq_(freq), start_(SDL_GetPerformanceCounter()) {}
    ~ScopedRendererTimer() { outMs_ += ticksToMs(SDL_GetPerformanceCounter() - start_, freq_); }

private:
    float& outMs_;
    Uint64 freq_ = 0;
    Uint64 start_ = 0;
};

/// Side-table for per-model emissive overrides set via `setModelEmissive`.
/// TODO(graphics): consume these inside `drawModel` when building the
/// material UBO — currently captured but unused.
std::unordered_map<int32_t, glm::vec4> g_emissiveOverrides;

struct StaticBatchKey
{
    MaterialIdInt materialId = 0;
    TexIdInt textureId = 0;
    bool useTexture = false;

    bool operator==(const StaticBatchKey& other) const noexcept
    {
        return materialId == other.materialId && textureId == other.textureId && useTexture == other.useTexture;
    }
};

struct StaticBatchKeyHash
{
    std::size_t operator()(const StaticBatchKey& key) const noexcept
    {
        std::size_t h = static_cast<std::size_t>(key.materialId);
        h ^= static_cast<std::size_t>(key.textureId) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
        h ^= static_cast<std::size_t>(key.useTexture ? 1u : 0u) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
        return h;
    }
};

struct StaticBatchBuildData
{
    StaticBatchKey key{};
    SDL_GPUTexture* texture = nullptr;
    bool useTexture = false;
    glm::vec4 materialDiffuse{0.8f, 0.8f, 0.8f, 1.0f};
    std::vector<Asset::Vertex> vertices;
    std::vector<uint32_t> indices;
};

const char* presentModeName(SDL_GPUPresentMode presentMode)
{
    switch (presentMode) {
    case SDL_GPU_PRESENTMODE_VSYNC:
        return "vsync";
    case SDL_GPU_PRESENTMODE_IMMEDIATE:
        return "immediate";
    case SDL_GPU_PRESENTMODE_MAILBOX:
        return "mailbox";
    default:
        return "unknown";
    }
}

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
    device_ = SDL_CreateGPUDevice(k_wantedFormats, false, nullptr);
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

bool NewRenderer::createGeometryPipeline()
{
    Boilerplate::ShaderInfo vertexShader{};
    vertexShader.path = "shaders-new/geometry.vert";
    vertexShader.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexShader.uniformBufferCount = 2;

    Boilerplate::ShaderInfo fragmentShader{};
    fragmentShader.path = "shaders-new/geometry.frag";
    fragmentShader.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentShader.samplerCount = 1;
    fragmentShader.uniformBufferCount = 2;

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

    geometryPipeline_ = Boilerplate::createGraphicsPipeline(
        device_, colorTarget_, shaderFormat_, vertexShader, fragmentShader, vertexLayout, true, true);

    return geometryPipeline_ != nullptr;
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
    lastAcquireMs_ = 0.0f;
    lastRecordMs_ = 0.0f;
    lastSubmitMs_ = 0.0f;
    lastFrameStats_ = {};
    lastFrameStats_.entityCmds = static_cast<Uint32>(entities_.size());
    lastFrameStats_.pointLights = static_cast<Uint32>(pointLights_.size());
    lastFrameStats_.skinnedInstances = static_cast<Uint32>(skinnedRenderer_.pendingInstanceCount());
    lastFrameStats_.presentMode = static_cast<Uint32>(presentMode_);

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
    {
        ScopedRendererTimer timer(lastFrameStats_.swapchainAcquireMs, freq);
        if (!SDL_AcquireGPUSwapchainTexture(cmd, window_, &swapchain, &width, &height)) {
            SDL_Log("NewRenderer::drawFrame: SDL_AcquireGPUSwapchainTexture failed: %s", SDL_GetError());
            SDL_CancelGPUCommandBuffer(cmd);
            return;
        }
    }

    if (!swapchain) {
        // Swapchain not ready (e.g. minimised) — drop the frame silently.
        lastFrameStats_.swapchainSkipped = 1;
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }
    lastFrameStats_.swapchainWidth = width;
    lastFrameStats_.swapchainHeight = height;

    {
        ScopedRendererTimer timer(lastFrameStats_.depthEnsureMs, freq);
        if (!ensureDepthTextureSize(width, height)) {
            SDL_Log("NewRenderer::drawFrame: ensureDepthTextureSize failed");
            SDL_CancelGPUCommandBuffer(cmd);
            return;
        }
    }

    {
        ScopedRendererTimer timer(lastFrameStats_.cameraUpdateMs, freq);
        setMainCamera(eye, yaw, pitch, roll, width, height);
    }

    if (staticBatchesDirty_) {
        ScopedRendererTimer timer(lastFrameStats_.staticBatchRebuildMs, freq);
        rebuildStaticBatches(cmd);
    }

    // Per-frame uploads (skinning palette/instances, etc.) happen BEFORE
    // the first render pass so the copy is sequenced ahead of the draws.
    if (skinnedRenderer_.pendingInstanceCount() > 0) {
        ScopedRendererTimer timer(lastFrameStats_.skinnedUploadMs, freq);
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        if (copyPass) {
            skinnedRenderer_.uploadFrame(cmd, copyPass);
            SDL_EndGPUCopyPass(copyPass);
        }
    }

    {
        ScopedRendererTimer timer(lastFrameStats_.geometryPassMs, freq);
        drawGeometryPass(swapchain, cmd);
    }
    {
        ScopedRendererTimer timer(lastFrameStats_.weaponPassMs, freq);
        drawWeaponPass(swapchain, cmd);
    }
    {
        ScopedRendererTimer timer(lastFrameStats_.uiPassMs, freq);
        drawUIPass(swapchain, cmd);
    }

    const Uint64 t2 = SDL_GetPerformanceCounter();
    lastRecordMs_ = ticksToMs(t2 - t1, freq);

    SDL_SubmitGPUCommandBuffer(cmd);
    lastFrameStats_.frameSubmitted = 1;

    const Uint64 t3 = SDL_GetPerformanceCounter();
    lastSubmitMs_ = ticksToMs(t3 - t2, freq);

    // TODO(graphics): if `pendingScreenshotPath_` is non-empty, schedule a
    // swapchain readback and write a PNG.  See `requestScreenshot` doc.
}

void NewRenderer::setMainCamera(glm::vec3 eye, float yaw, float pitch, float roll, Uint32 width, Uint32 height)
{
    camera_.setEye(eye);
    camera_.setTarget(pitch, yaw, roll);
    camera_.setAspect(static_cast<float>(width), static_cast<float>(height));
    const float aspect = height == 0 ? 1.0f : static_cast<float>(width) / static_cast<float>(height);
    camera_.setFov(verticalFovDegreesFromHorizontal(mainHorizontalFovDegrees, aspect));
    camera_.computeViewProjectionMatrix();
}

void NewRenderer::drawGeometryPass(SDL_GPUTexture* swapchain, SDL_GPUCommandBuffer* cmd)
{
    SDL_GPUColorTargetInfo colorTarget =
        Boilerplate::makeColorTargetClear(swapchain, SDL_FColor{.r = 0.08f, .g = 0.08f, .b = 0.12f, .a = 1.0f});

    SDL_GPURenderPass* geometryPass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, &depthTarget_);
    SDL_BindGPUGraphicsPipeline(geometryPass, geometryPipeline_);

    const glm::mat4 viewProjection = camera_.getViewProjectionMatrix();
    SDL_PushGPUVertexUniformData(cmd, 0, &viewProjection, sizeof(glm::mat4));
    drawWorldModelInstances(geometryPass, cmd);
    drawEntityModels(geometryPass, cmd);

    if (skinnedRenderer_.pendingInstanceCount() > 0)
        drawSkinnedModels(geometryPass, cmd);

    // drawWeapon(geometryPass, cmd);

    SDL_EndGPURenderPass(geometryPass);
}

void NewRenderer::drawWeaponPass(SDL_GPUTexture* swapchain, SDL_GPUCommandBuffer* cmd)
{
    if (!weapon_.visible)
        return;
    if (weapon_.modelIndex < 0 || static_cast<size_t>(weapon_.modelIndex) >= Asset::modelInstances_.size())
        return;

    SDL_GPUColorTargetInfo colorTarget = Boilerplate::makeColorTargetLoad(swapchain);

    SDL_GPUDepthStencilTargetInfo depthInfo = depthTarget_; // copy
    depthInfo.load_op = SDL_GPU_LOADOP_CLEAR;               // override to clear
    depthInfo.clear_depth = 1.0f;

    SDL_GPURenderPass* geometryPass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, &depthInfo);
    SDL_BindGPUGraphicsPipeline(geometryPass, geometryPipeline_);

    const glm::mat4 viewProjection = camera_.getViewProjectionMatrix();
    SDL_PushGPUVertexUniformData(cmd, 0, &viewProjection, sizeof(glm::mat4));

    drawWeapon(geometryPass, cmd);

    SDL_EndGPURenderPass(geometryPass);
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

    lastFrameStats_.weaponDrawn = 1;
    drawModel(weaponModelId, weapon_.transform, renderPass, cmd);
}

void NewRenderer::drawSkinnedModels(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd)
{
    skinnedRenderer_.draw(renderPass, cmd);
}

void NewRenderer::drawWorldModelInstances(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd)
{
    if (!staticBatches_.empty()) {
        lastFrameStats_.worldInstances = staticBatchWorldInstances_;
        drawStaticBatches(renderPass, cmd);
        return;
    }

    for (const auto& mInstance : Asset::modelInstances_) {
        if (!mInstance.drawInScenePass)
            continue;
        ++lastFrameStats_.worldInstances;
        drawModel(mInstance.modelId_, mInstance.transform_, renderPass, cmd, false);
    }
}

void NewRenderer::drawStaticBatches(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd)
{
    const glm::mat4 identity{1.0f};
    SDL_PushGPUVertexUniformData(cmd, 1, &identity, sizeof(glm::mat4));

    for (const StaticBatch& batch : staticBatches_) {
        if (batch.vertexBuffer == nullptr || batch.indexBuffer == nullptr || batch.indexCount == 0)
            continue;

        SDL_GPUTexture* texture = batch.texture != nullptr ? batch.texture : texture_;
        SDL_GPUTextureSamplerBinding textureBinding = Boilerplate::makeTextureSamplerBinding(texture, sampler_);
        SDL_BindGPUFragmentSamplers(renderPass, 0, &textureBinding, 1);
        ++lastFrameStats_.textureBinds;

        const Uint32 useTextureUniform = batch.useTexture ? 1u : 0u;
        SDL_PushGPUFragmentUniformData(cmd, 0, &batch.materialDiffuse, sizeof(batch.materialDiffuse));
        SDL_PushGPUFragmentUniformData(cmd, 1, &useTextureUniform, sizeof(useTextureUniform));
        ++lastFrameStats_.materialBinds;

        SDL_GPUBufferBinding vertexBufferBinding{};
        vertexBufferBinding.buffer = batch.vertexBuffer;
        vertexBufferBinding.offset = 0;
        SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);

        SDL_GPUBufferBinding indexBufferBinding{};
        indexBufferBinding.buffer = batch.indexBuffer;
        indexBufferBinding.offset = 0;
        SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_DrawGPUIndexedPrimitives(renderPass, batch.indexCount, 1, 0, 0, 0);
        ++lastFrameStats_.staticBatchDraws;
        ++lastFrameStats_.meshDraws;
        ++lastFrameStats_.indexedDraws;
        lastFrameStats_.triangles += batch.triangleCount;
        lastFrameStats_.staticTriangles += batch.triangleCount;
    }
}

void NewRenderer::drawEntityModels(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd)
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
        ++lastFrameStats_.entityDraws;
        drawModel(modelId, entityCmd.worldTransform, renderPass, cmd);
    }
}

void NewRenderer::drawModel(ModelIdInt modelId,
                            const glm::mat4& modelTransform,
                            SDL_GPURenderPass* renderPass,
                            SDL_GPUCommandBuffer* cmd,
                            bool countDynamicDraws)
{
    Asset::Model& model = Asset::models_.at(modelId);
    ++lastFrameStats_.modelDraws;
    for (auto& element : model.modelElements_) {
        const Asset::Material* material = nullptr;
        if (Asset::materials_.contains(element.materialId_))
            material = &Asset::materials_.at(element.materialId_);

        SDL_GPUTexture* texture = nullptr;
        if (material != nullptr) {
            const TexIdInt texId = material->texId_[0];
            if (Asset::textures_.contains(texId))
                texture = Asset::textures_.at(texId).tex;
        }

        const bool useTexture = texture != nullptr || material == nullptr || !material->hasPhongData_;
        if (texture == nullptr)
            texture = texture_;

        SDL_GPUTextureSamplerBinding textureBinding = Boilerplate::makeTextureSamplerBinding(texture, sampler_);
        SDL_BindGPUFragmentSamplers(renderPass, 0, &textureBinding, 1);
        ++lastFrameStats_.textureBinds;

        glm::vec4 materialDiffuse{0.8f, 0.8f, 0.8f, 1.0f};
        if (material != nullptr)
            materialDiffuse = glm::vec4(material->kDiffuse_, 1.0f);
        Uint32 useTextureUniform = useTexture ? 1u : 0u;
        SDL_PushGPUFragmentUniformData(cmd, 0, &materialDiffuse, sizeof(materialDiffuse));
        SDL_PushGPUFragmentUniformData(cmd, 1, &useTextureUniform, sizeof(useTextureUniform));
        ++lastFrameStats_.materialBinds;

        glm::mat4 modelElementMatrix = modelTransform * element.cachedTransform_;
        SDL_PushGPUVertexUniformData(cmd, 1, &modelElementMatrix, sizeof(glm::mat4));

        Asset::Mesh& mesh = Asset::meshes_.at(element.meshId_);
        drawMesh(renderPass, mesh);
        if (countDynamicDraws)
            ++lastFrameStats_.dynamicDraws;
    }
}

void NewRenderer::drawMesh(SDL_GPURenderPass* renderPass, const Asset::Mesh& mesh)
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
    ++lastFrameStats_.meshDraws;
    ++lastFrameStats_.indexedDraws;
    lastFrameStats_.triangles += indexCount / 3;
}

void NewRenderer::releaseStaticBatches()
{
    if (device_ == nullptr) {
        staticBatches_.clear();
        staticBatchWorldInstances_ = 0;
        staticBatchTriangles_ = 0;
        return;
    }

    for (StaticBatch& batch : staticBatches_) {
        if (batch.vertexBuffer != nullptr)
            SDL_ReleaseGPUBuffer(device_, batch.vertexBuffer);
        if (batch.indexBuffer != nullptr)
            SDL_ReleaseGPUBuffer(device_, batch.indexBuffer);
    }
    staticBatches_.clear();
    staticBatchWorldInstances_ = 0;
    staticBatchTriangles_ = 0;
}

void NewRenderer::rebuildStaticBatches(SDL_GPUCommandBuffer* cmd)
{
    staticBatchesDirty_ = false;
    releaseStaticBatches();
    if (device_ == nullptr || cmd == nullptr)
        return;

    std::vector<StaticBatchBuildData> buildBatches;
    std::unordered_map<StaticBatchKey, std::size_t, StaticBatchKeyHash> batchByKey;

    for (const Asset::ModelInstance& instance : Asset::modelInstances_) {
        if (!instance.drawInScenePass)
            continue;

        const auto modelIt = Asset::models_.find(instance.modelId_);
        if (modelIt == Asset::models_.end())
            continue;

        ++staticBatchWorldInstances_;
        const Asset::Model& model = modelIt->second;
        for (const Asset::ModelElement& element : model.modelElements_) {
            const auto meshIt = Asset::meshes_.find(element.meshId_);
            if (meshIt == Asset::meshes_.end())
                continue;

            const Asset::Material* material = nullptr;
            if (const auto materialIt = Asset::materials_.find(element.materialId_);
                materialIt != Asset::materials_.end())
                material = &materialIt->second;

            TexIdInt textureId = 0;
            SDL_GPUTexture* texture = nullptr;
            if (material != nullptr) {
                const TexIdInt candidateTexId = material->texId_[0];
                const auto textureIt = Asset::textures_.find(candidateTexId);
                if (textureIt != Asset::textures_.end() && textureIt->second.tex != nullptr) {
                    textureId = candidateTexId;
                    texture = textureIt->second.tex;
                }
            }

            const bool useTexture = texture != nullptr || material == nullptr || !material->hasPhongData_;
            const StaticBatchKey key{
                .materialId = element.materialId_,
                .textureId = textureId,
                .useTexture = useTexture,
            };

            std::size_t batchIndex = 0;
            const auto batchIt = batchByKey.find(key);
            if (batchIt == batchByKey.end()) {
                StaticBatchBuildData build{};
                build.key = key;
                build.texture = texture;
                build.useTexture = useTexture;
                if (material != nullptr)
                    build.materialDiffuse = glm::vec4(material->kDiffuse_, 1.0f);
                batchIndex = buildBatches.size();
                batchByKey.emplace(key, batchIndex);
                buildBatches.push_back(std::move(build));
            } else {
                batchIndex = batchIt->second;
            }

            StaticBatchBuildData& batch = buildBatches[batchIndex];
            const Asset::Mesh& mesh = meshIt->second;
            const uint32_t baseVertex = static_cast<uint32_t>(batch.vertices.size());
            const glm::mat4 elementTransform = instance.transform_ * element.cachedTransform_;
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(elementTransform)));

            batch.vertices.reserve(batch.vertices.size() + mesh.vertexData_.size());
            for (const Asset::Vertex& vertex : mesh.vertexData_) {
                Asset::Vertex baked = vertex;
                baked.position = glm::vec3(elementTransform * glm::vec4(vertex.position, 1.0f));
                const glm::vec3 transformedNormal = normalMatrix * vertex.normal;
                if (glm::dot(transformedNormal, transformedNormal) > 1e-10f)
                    baked.normal = glm::normalize(transformedNormal);
                batch.vertices.push_back(baked);
            }

            batch.indices.reserve(batch.indices.size() + mesh.indexData_.size());
            for (uint32_t index : mesh.indexData_)
                batch.indices.push_back(baseVertex + index);
        }
    }

    std::vector<Boilerplate::BufferUpload> uploads;
    staticBatches_.reserve(buildBatches.size());
    for (const StaticBatchBuildData& build : buildBatches) {
        if (build.vertices.empty() || build.indices.empty())
            continue;

        const size_t vertexBytes = build.vertices.size() * sizeof(Asset::Vertex);
        const size_t indexBytes = build.indices.size() * sizeof(uint32_t);

        StaticBatch batch{};
        batch.vertexBuffer = Boilerplate::createBuffer(device_, vertexBytes, SDL_GPU_BUFFERUSAGE_VERTEX);
        batch.indexBuffer = Boilerplate::createBuffer(device_, indexBytes, SDL_GPU_BUFFERUSAGE_INDEX);
        if (batch.vertexBuffer == nullptr || batch.indexBuffer == nullptr) {
            if (batch.vertexBuffer != nullptr)
                SDL_ReleaseGPUBuffer(device_, batch.vertexBuffer);
            if (batch.indexBuffer != nullptr)
                SDL_ReleaseGPUBuffer(device_, batch.indexBuffer);
            continue;
        }

        batch.texture = build.texture;
        batch.useTexture = build.useTexture;
        batch.materialDiffuse = build.materialDiffuse;
        batch.indexCount = static_cast<Uint32>(build.indices.size());
        batch.triangleCount = static_cast<Uint32>(build.indices.size() / 3u);
        staticBatchTriangles_ += batch.triangleCount;

        uploads.push_back({batch.vertexBuffer, build.vertices.data(), static_cast<Uint32>(vertexBytes)});
        uploads.push_back({batch.indexBuffer, build.indices.data(), static_cast<Uint32>(indexBytes)});
        staticBatches_.push_back(batch);
    }

    Boilerplate::uploadBuffers(device_, cmd, uploads);
}

bool NewRenderer::ensureDepthTextureSize(Uint32 width, Uint32 height)
{
    if (depthTarget_.texture && depthWidth_ == width && depthHeight_ == height)
        return true;

    if (depthTarget_.texture) {
        SDL_ReleaseGPUTexture(device_, depthTarget_.texture);
        depthTarget_.texture = nullptr;
    }

    depthTarget_ = Boilerplate::makeDepthTarget(Boilerplate::createDepthTexture(device_, width, height));

    if (!depthTarget_.texture)
        return false;

    depthWidth_ = width;
    depthHeight_ = height;
    return true;
}

void NewRenderer::drawUIPass(SDL_GPUTexture* swapchain, SDL_GPUCommandBuffer* cmd)
{
    ImDrawData* drawData = imguiEnabled ? ImGui::GetDrawData() : nullptr;
    const bool drawImgui = drawData != nullptr && drawData->CmdListsCount > 0;
    const bool drawHudTexture = hudTexture_ != nullptr;
    if (!drawHudTexture && !drawImgui)
        return;

    const Uint64 freq = SDL_GetPerformanceFrequency();
    if (drawImgui) {
        ScopedRendererTimer timer(lastFrameStats_.imguiPrepareMs, freq);
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, cmd);
    }

    SDL_GPUColorTargetInfo uiColorTarget = Boilerplate::makeColorTargetLoad(swapchain);

    SDL_GPURenderPass* uiPass = SDL_BeginGPURenderPass(cmd, &uiColorTarget, 1, nullptr);

    if (drawHudTexture) {
        ScopedRendererTimer timer(lastFrameStats_.hudDrawMs, freq);
        SDL_BindGPUGraphicsPipeline(uiPass, hudPipeline_);
        drawHud(uiPass);
    }

    if (drawImgui) {
        ScopedRendererTimer timer(lastFrameStats_.imguiDrawMs, freq);
        ImGui_ImplSDLGPU3_RenderDrawData(drawData, cmd, uiPass);
    }

    SDL_EndGPURenderPass(uiPass);
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

        releaseStaticBatches();

        if (depthTarget_.texture)
            SDL_ReleaseGPUTexture(device_, depthTarget_.texture);

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
        if (sampler_)
            SDL_ReleaseGPUSampler(device_, sampler_);
        if (hudSampler_)
            SDL_ReleaseGPUSampler(device_, hudSampler_);
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
    depthTarget_.texture = nullptr;
    texture_ = nullptr;
    sampler_ = nullptr;
    hudTexture_ = nullptr;
    hudSampler_ = nullptr;
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
    for (auto& element : model.modelElements_) {
        createMeshBuffers(element.meshId_);
        Asset::Mesh& mesh = Asset::meshes_[element.meshId_];
        uploads.push_back({mesh.vBufferInfo_.gpuBuff, mesh.vBufferInfo_.srcData, mesh.vBufferInfo_.bufferSize});
        uploads.push_back({mesh.iBufferInfo_.gpuBuff, mesh.iBufferInfo_.srcData, mesh.iBufferInfo_.bufferSize});

        MaterialIdInt matId = element.materialId_;
        if (!Asset::materials_.contains(matId))
            continue;

        Asset::Material& mat = Asset::materials_.at(matId);
        TexIdInt texId = mat.texId_[0];

        if (texId == 0 || !Asset::textures_.contains(texId))
            continue;

        Asset::Texture& tex = Asset::textures_.at(texId);
        if (tex.tex == nullptr && tex.tex_raw != nullptr && tex.width > 0 && tex.height > 0) {
            tex.tex = Boilerplate::createTextureRGBA8(
                device_, static_cast<Uint32>(tex.width), static_cast<Uint32>(tex.height), tex.tex_raw);
            stbi_image_free(tex.tex_raw);
            tex.tex_raw = nullptr;
        }
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) {
        SDL_Log("NewRenderer: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return -1;
    }

    Boilerplate::uploadBuffers(device_, cmd, uploads);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(device_);

    staticBatchesDirty_ = true;
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
    pointLights_ = std::move(pointLights);
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
    staticBatchesDirty_ = true;
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

bool NewRenderer::setPresentMode(SDL_GPUPresentMode presentMode)
{
    if (!device_ || !window_)
        return false;

    if (!SDL_WindowSupportsGPUPresentMode(device_, window_, presentMode)) {
        SDL_Log("NewRenderer: requested present mode is unsupported (%d)", static_cast<int>(presentMode));
        return false;
    }

    if (!SDL_SetGPUSwapchainParameters(device_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, presentMode)) {
        SDL_Log("NewRenderer: SDL_SetGPUSwapchainParameters failed: %s", SDL_GetError());
        return false;
    }

    vsyncEnabled_ = presentMode == SDL_GPU_PRESENTMODE_VSYNC;
    presentMode_ = presentMode;
    SDL_Log("NewRenderer: present mode = %s", presentModeName(presentMode));
    return true;
}

bool NewRenderer::setVSync(bool enabled)
{
    if (enabled)
        return setPresentMode(SDL_GPU_PRESENTMODE_VSYNC);

    if (device_ && window_ && SDL_WindowSupportsGPUPresentMode(device_, window_, SDL_GPU_PRESENTMODE_MAILBOX))
        return setPresentMode(SDL_GPU_PRESENTMODE_MAILBOX);
    return setPresentMode(SDL_GPU_PRESENTMODE_IMMEDIATE);
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
