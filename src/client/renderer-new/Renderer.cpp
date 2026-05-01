/// @file Renderer.cpp
/// @brief Implementation of the work-in-progress NewRenderer.

#include "Renderer.hpp"

#include "Asset.hpp"
#include "AssetLoader.hpp"
#include "Boilerplate.hpp"

#include <backends/imgui_impl_sdlgpu3.h>
#include <cstddef>
#include <glm/ext/matrix_transform.hpp>
#include <imgui.h>
#include <iostream>
#include <vector>

bool NewRenderer::supports(RendererFeature feature) const
{
    switch (feature) {
    case RendererFeature::Init:
    case RendererFeature::DrawFrame:
    case RendererFeature::Quit:
        return true;
    default:
        return false;
    }
}

bool NewRenderer::init(SDL_Window* window)
{
    window_ = window;
    ownsDevice_ = true;
    ownsWindowClaim_ = true;

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

    return initCommon();
}

bool NewRenderer::init(SDL_Window* window, SDL_GPUDevice* sharedDevice)
{
    window_ = window;
    device_ = sharedDevice;
    ownsDevice_ = false;
    ownsWindowClaim_ = false;

    return initCommon();
}

bool NewRenderer::initCommon()
{
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

    sampler_ = Boilerplate::createLinearRepeatSampler(device_);
    if (!sampler_) {
        SDL_Log("NewRenderer: failed to create sampler: %s", SDL_GetError());
        return false;
    }

    texture_ = Boilerplate::loadTexture(device_, "ropfyx6etjdb1.jpg");
    if (!texture_) {
        SDL_Log("NewRenderer: failed to load texture");
        return false;
    }

    camera_ = NewCamera();

    return loadSceneAssets();
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

    Boilerplate::VertexInputLayout vertexLayout{};
    vertexLayout.vertexPitch = sizeof(Vertex);
    vertexLayout.attributes = {
        Boilerplate::makeAttribute(0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, position)),
        Boilerplate::makeAttribute(1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, normal)),
        Boilerplate::makeAttribute(2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, texUV)),
    };

    geometryPipeline_ = Boilerplate::createGraphicsPipeline(
        device_, window_, shaderFormat_, vertexShader, fragmentShader, vertexLayout, true);

    return geometryPipeline_ != nullptr;
}

bool NewRenderer::loadSceneAssets()
{
    std::cout << "loading models" << std::endl;
    AssetLoader::loadModelsList();
    std::cout << "loaded models" << std::endl;

    std::vector<Boilerplate::BufferUpload> uploads;

    for (const auto& modelPair : Asset::models_) {
        for (auto& element : modelPair.second.modelElements_) {
            createMeshBuffers(element.meshId_);
            Asset::Mesh& mesh = Asset::meshes_[element.meshId_];
            uploads.push_back({mesh.vBufferInfo_.gpuBuff, mesh.vBufferInfo_.srcData, mesh.vBufferInfo_.bufferSize});
            uploads.push_back({mesh.iBufferInfo_.gpuBuff, mesh.iBufferInfo_.srcData, mesh.iBufferInfo_.bufferSize});
        }
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) {
        SDL_Log("NewRenderer: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return false;
    }

    Boilerplate::uploadBuffers(device_, cmd, uploads);
    SDL_SubmitGPUCommandBuffer(cmd);

    return true;
}

void NewRenderer::createMeshBuffers(MeshIdInt meshId)
{
    Asset::Mesh& mesh = Asset::meshes_.at(meshId);

    const size_t vertexBufferSize = mesh.vertexData_.size() * sizeof(Vertex);
    const size_t indexBufferSize = mesh.indexData_.size() * sizeof(Uint32);

    mesh.vBufferInfo_.bufferSize = vertexBufferSize;
    mesh.vBufferInfo_.gpuBuff = Boilerplate::createBuffer(device_, vertexBufferSize, SDL_GPU_BUFFERUSAGE_VERTEX);
    mesh.vBufferInfo_.srcData = mesh.vertexData_.data();

    mesh.iBufferInfo_.bufferSize = indexBufferSize;
    mesh.iBufferInfo_.gpuBuff = Boilerplate::createBuffer(device_, indexBufferSize, SDL_GPU_BUFFERUSAGE_INDEX);
    mesh.iBufferInfo_.srcData = mesh.indexData_.data();
}

void NewRenderer::drawFrame(glm::vec3 eye, float yaw, float pitch, float /*roll*/)
{
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) {
        SDL_Log("NewRenderer::drawFrame: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return;
    }

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, window_, &swapchain, &width, &height) || !swapchain) {
        SDL_Log("NewRenderer::drawFrame: SDL_AcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    if (!ensureDepthTexture(width, height)) {
        SDL_Log("NewRenderer::drawFrame: ensureDepthTexture failed");
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    camera_.setEye(eye);
    camera_.setTarget(pitch, yaw, 0.0f);
    camera_.setAspect(static_cast<float>(width), static_cast<float>(height));
    camera_.computeViewProjectionMatrix();

    const glm::mat4 viewProjection = camera_.getViewProjectionMatrix();
    SDL_PushGPUVertexUniformData(cmd, 0, &viewProjection, sizeof(glm::mat4));

    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData)
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, cmd);

    SDL_GPUColorTargetInfo colorTarget =
        Boilerplate::makeColorTarget(swapchain, SDL_FColor{.r = 0.08f, .g = 0.08f, .b = 0.12f, .a = 1.0f});

    SDL_GPUDepthStencilTargetInfo depthTarget = Boilerplate::makeDepthTarget(depthTexture_);

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, &depthTarget);
    SDL_BindGPUGraphicsPipeline(pass, geometryPipeline_);

    SDL_GPUTextureSamplerBinding textureBinding = Boilerplate::makeTextureSamplerBinding(texture_, sampler_);
    SDL_BindGPUFragmentSamplers(pass, 0, &textureBinding, 1);

    for (const auto& modelPair : Asset::models_) {
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        modelMatrix = glm::scale(modelMatrix, glm::vec3(10000.0f));

        for (auto& element : modelPair.second.modelElements_) {
            glm::mat4 modelElementMatrix = modelMatrix * element.cachedTransform_;
            SDL_PushGPUVertexUniformData(cmd, 1, &modelElementMatrix, sizeof(glm::mat4));
            Asset::Mesh& mesh = Asset::meshes_.at(element.meshId_);
            drawMesh(pass, mesh);
        }
    }

    if (drawData)
        ImGui_ImplSDLGPU3_RenderDrawData(drawData, cmd, pass);

    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
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

bool NewRenderer::ensureDepthTexture(Uint32 width, Uint32 height)
{
    if (depthTexture_ && depthWidth_ == width && depthHeight_ == height)
        return true;

    if (depthTexture_) {
        SDL_ReleaseGPUTexture(device_, depthTexture_);
        depthTexture_ = nullptr;
    }

    depthTexture_ = Boilerplate::createDepthTexture(device_, width, height);
    if (!depthTexture_)
        return false;

    depthWidth_ = width;
    depthHeight_ = height;
    return true;
}

void NewRenderer::quit()
{
    if (device_) {
        SDL_WaitForGPUIdle(device_);

        if (depthTexture_)
            SDL_ReleaseGPUTexture(device_, depthTexture_);

        for (auto& meshPair : Asset::meshes_) {
            Asset::Mesh& mesh = meshPair.second;

            if (mesh.vBufferInfo_.gpuBuff)
                SDL_ReleaseGPUBuffer(device_, mesh.vBufferInfo_.gpuBuff);
            if (mesh.iBufferInfo_.gpuBuff)
                SDL_ReleaseGPUBuffer(device_, mesh.iBufferInfo_.gpuBuff);

            mesh.vBufferInfo_ = {};
            mesh.iBufferInfo_ = {};
        }

        if (geometryPipeline_)
            SDL_ReleaseGPUGraphicsPipeline(device_, geometryPipeline_);
        if (sampler_)
            SDL_ReleaseGPUSampler(device_, sampler_);
        if (texture_)
            SDL_ReleaseGPUTexture(device_, texture_);

        if (ownsDevice_) {
            ImGui_ImplSDLGPU3_Shutdown();
            if (ownsWindowClaim_)
                SDL_ReleaseWindowFromGPUDevice(device_, window_);
            SDL_DestroyGPUDevice(device_);
        }
    }

    window_ = nullptr;
    device_ = nullptr;
    shaderFormat_ = SDL_GPU_SHADERFORMAT_INVALID;
    ownsDevice_ = false;
    ownsWindowClaim_ = false;

    geometryPipeline_ = nullptr;
    depthTexture_ = nullptr;
    texture_ = nullptr;
    sampler_ = nullptr;
    depthWidth_ = 0;
    depthHeight_ = 0;
}
