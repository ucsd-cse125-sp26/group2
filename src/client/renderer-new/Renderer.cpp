#include "Renderer.hpp"

#include "Asset.hpp"
#include "AssetLoader.hpp"
#include "Boilerplate.hpp"
#include "Camera.hpp"

#include <backends/imgui_impl_sdlgpu3.h>
#include <cmath>
#include <filesystem>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <imgui.h>
#include <iostream>
#include <vector>

namespace
{

/// @brief Select active format, prefer SPIR-V, fallback to MSL if avaliable
SDL_GPUShaderFormat selectFormat(SDL_GPUDevice* device)
{
    const SDL_GPUShaderFormat kAvailableFormats = SDL_GetGPUShaderFormats(device);

    if (kAvailableFormats & SDL_GPU_SHADERFORMAT_SPIRV)
        return SDL_GPU_SHADERFORMAT_SPIRV;

#ifdef HAVE_MSL_SHADERS
    if (kAvailableFormats & SDL_GPU_SHADERFORMAT_MSL)
        return SDL_GPU_SHADERFORMAT_MSL;
#endif

    return SDL_GPU_SHADERFORMAT_INVALID;
}

ImGui_ImplSDLGPU3_InitInfo createImGuiInfo(SDL_GPUDevice* device, SDL_Window* window)
{
    ImGui_ImplSDLGPU3_InitInfo imguiInfo{};
    imguiInfo.Device = device;
    imguiInfo.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
    imguiInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    return imguiInfo;
}

SDL_GPUGraphicsPipeline* createGeometryPipeline(SDL_GPUDevice* device,
                                                SDL_Window* window,
                                                SDL_GPUShaderFormat shaderFormat,
                                                const std::string& k_shadersDir)
{
    const std::string vertexShaderPath = (std::filesystem::path(k_shadersDir) / "geometry.vert").string();
    Uint32 vertexShaderSamplerCount = 0;
    Uint32 vertexShaderUniformBufferCount = 2;
    Uint32 vertexShaderStorageBufferCount = 0;
    Uint32 vertexShaderStorageTextureCount = 0;

    const std::string fragmentShaderPath = (std::filesystem::path(k_shadersDir) / "geometry.frag").string();
    Uint32 fragmentShaderSamplerCount = 0;
    Uint32 fragmentShaderUniformBufferCount = 0;
    Uint32 fragmentShaderStorageBufferCount = 0;
    Uint32 fragmentShaderStorageTextureCount = 0;

    SDL_GPUShader* vertexShader = Boilerplate::loadShader(device,
                                                          vertexShaderPath.c_str(),
                                                          shaderFormat,
                                                          SDL_GPU_SHADERSTAGE_VERTEX,
                                                          vertexShaderSamplerCount,
                                                          vertexShaderUniformBufferCount,
                                                          vertexShaderStorageBufferCount,
                                                          vertexShaderStorageTextureCount);
    SDL_GPUShader* fragmentShader = Boilerplate::loadShader(device,
                                                            fragmentShaderPath.c_str(),
                                                            shaderFormat,
                                                            SDL_GPU_SHADERSTAGE_FRAGMENT,
                                                            fragmentShaderSamplerCount,
                                                            fragmentShaderUniformBufferCount,
                                                            fragmentShaderStorageBufferCount,
                                                            fragmentShaderStorageTextureCount);

    if (!vertexShader || !fragmentShader) {
        SDL_ReleaseGPUShader(device, vertexShader);
        SDL_ReleaseGPUShader(device, fragmentShader);
        return nullptr;
    }

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device, window);

    std::vector<SDL_GPUVertexBufferDescription> vertexBufferDescriptions;

    SDL_GPUVertexBufferDescription vBufferDescrFullInterleaved;
    vBufferDescrFullInterleaved.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vBufferDescrFullInterleaved.instance_step_rate = 0;
    vBufferDescrFullInterleaved.pitch = sizeof(Vertex);
    vBufferDescrFullInterleaved.slot = 0;

    vertexBufferDescriptions.push_back(vBufferDescrFullInterleaved);

    std::vector<SDL_GPUVertexAttribute> vertexAttributes;

    SDL_GPUVertexAttribute vertexAttribPos{};
    vertexAttribPos.buffer_slot = 0;
    vertexAttribPos.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertexAttribPos.location = 0;
    vertexAttribPos.offset = offsetof(Vertex, position);

    SDL_GPUVertexAttribute vertexAttribNorm{};
    vertexAttribNorm.buffer_slot = 0;
    vertexAttribNorm.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertexAttribNorm.location = 1;
    vertexAttribNorm.offset = offsetof(Vertex, normal);

    SDL_GPUVertexAttribute vertexAttribUV{};
    vertexAttribUV.buffer_slot = 0;
    vertexAttribUV.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertexAttribUV.location = 2;
    vertexAttribUV.offset = offsetof(Vertex, texUV);

    vertexAttributes.push_back(vertexAttribPos);
    vertexAttributes.push_back(vertexAttribNorm);
    vertexAttributes.push_back(vertexAttribUV);

    SDL_GPUVertexInputState vertexInputState{};
    vertexInputState.num_vertex_buffers = static_cast<Uint32>(vertexBufferDescriptions.size());
    vertexInputState.vertex_buffer_descriptions = vertexBufferDescriptions.data();
    vertexInputState.num_vertex_attributes = static_cast<Uint32>(vertexAttributes.size());
    vertexInputState.vertex_attributes = vertexAttributes.data();

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vertexShader;
    pci.fragment_shader = fragmentShader;
    pci.vertex_input_state = vertexInputState;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &colorTarget;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;

    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);

    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);

    return pipeline;
}

} // namespace

bool NewRenderer::supports(RendererFeature feature) const
{
    // As the graphics team re-implements more of the pipeline here, flip each
    // feature on below. Anything left as `false` will fall through to the
    // legacy renderer via the HybridRenderer dispatcher.
    switch (feature) {
    case RendererFeature::Init:
    case RendererFeature::DrawFrame:
    case RendererFeature::Quit:
        return true;
    default:
        return false;
    }
}

bool NewRenderer::init(SDL_Window* win)
{
    window_ = win;
    ownsDevice_ = true;
    ownsWindowClaim_ = true;

    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (!device_) {
        SDL_Log("NewRenderer: SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    SDL_Log("NewRenderer: GPU driver = %s", SDL_GetGPUDeviceDriver(device_));

    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
        SDL_Log("NewRenderer: SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    // Standalone path also initialises the ImGui GPU backend. The shared-device
    // path (used by HybridRenderer) skips this because the legacy renderer owns it.
    ImGui_ImplSDLGPU3_InitInfo imguiInfo = createImGuiInfo(device_, window_);
    if (!ImGui_ImplSDLGPU3_Init(&imguiInfo)) {
        SDL_Log("NewRenderer: ImGui_ImplSDLGPU3_Init failed");
        return false;
    }

    return initCommon(win);
}

bool NewRenderer::init(SDL_Window* win, SDL_GPUDevice* sharedDevice)
{
    window_ = win;
    device_ = sharedDevice;
    ownsDevice_ = false;      ///< Device lifetime belongs to the other renderer.
    ownsWindowClaim_ = false; ///< Window claim also belongs to the other renderer.
    // ImGui GPU backend is initialised by the device owner -- don't double-init here.
    return initCommon(win);
}

bool NewRenderer::initCommon(SDL_Window* /*win*/)
{
    shaderFormat_ = selectFormat(device_);
    if (shaderFormat_ == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_Log("NewRenderer: no supported shader format (got 0x%x)",
                static_cast<unsigned>(SDL_GetGPUShaderFormats(device_)));
        return false;
    }

    shadersDir_ = "shaders-new";
    pipeline_ = createGeometryPipeline(device_, window_, shaderFormat_, shadersDir_);
    if (!pipeline_) {
        SDL_Log("NewRenderer: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    camera_ = NewCamera();

    std::cout << "loading models" << std::endl;
    AssetLoader::loadModelsList();
    std::cout << "loaded models" << std::endl;

    std::vector<Asset::GeoBufferInfo> geoBuffers;
    // geoBuffers.push_back(vBufferInfo_);
    // geoBuffers.push_back(iBufferInfo_);
    // Uint32 numVertices = sizeof(cubeVertexData) / sizeof(Vertex);
    // Uint32 numIndices = sizeof(indices) / sizeof(Uint32);
    //
    // vBufferInfo_.bufferSize = numVertices * sizeof(Vertex);
    // vBufferInfo_.gpuBuff = createGPUBuffer(vBufferInfo_.bufferSize, SDL_GPU_BUFFERUSAGE_VERTEX);
    // vBufferInfo_.srcData = cubeVertexData;
    //
    // iBufferInfo_.bufferSize = numIndices * sizeof(Uint32);
    // iBufferInfo_.gpuBuff = createGPUBuffer(iBufferInfo_.bufferSize, SDL_GPU_BUFFERUSAGE_INDEX);
    // iBufferInfo_.srcData = indices;

    for (auto modelPair : Asset::models_) {
        MeshIdInt meshId = modelPair.second.meshId_;
        Asset::Mesh& mesh = Asset::meshes_[meshId];

        std::cout << "meshId:" << meshId << std::endl;

        genMeshBuffers(meshId);
        geoBuffers.push_back(mesh.vBufferInfo_);
        geoBuffers.push_back(mesh.iBufferInfo_);
    }
    std::cout << "0" << std::endl;

    SDL_GPUCommandBuffer* cmdCopyBuff = SDL_AcquireGPUCommandBuffer(device_);
    // SDL_GPUCopyPass* vaoCopyPass = SDL_BeginGPUCopyPass(cmdCopyBuff);
    std::cout << "1" << std::endl;

    uploadDataToGPUBuffer(cmdCopyBuff, geoBuffers);
    std::cout << "2" << std::endl;

    SDL_SubmitGPUCommandBuffer(cmdCopyBuff);
    std::cout << "3" << std::endl;

    return true;
}

void NewRenderer::genMeshBuffers(const MeshIdInt meshId)
{
    std::cout << "genMeshBuffers" << std::endl;
    Asset::Mesh& mesh = Asset::meshes_.at(meshId);

    std::cout << "getting sizes" << std::endl;
    Uint32 numVertices = mesh.vertexData_.size();
    Uint32 numIndices = mesh.indexData_.size();

    std::cout << "setting buffer Info" << std::endl;
    mesh.vBufferInfo_.bufferSize = numVertices * sizeof(Vertex);
    mesh.vBufferInfo_.gpuBuff = createGPUBuffer(mesh.vBufferInfo_.bufferSize, SDL_GPU_BUFFERUSAGE_VERTEX);
    mesh.vBufferInfo_.srcData = mesh.vertexData_.data();

    mesh.iBufferInfo_.bufferSize = numIndices * sizeof(Uint32);
    mesh.iBufferInfo_.gpuBuff = createGPUBuffer(mesh.iBufferInfo_.bufferSize, SDL_GPU_BUFFERUSAGE_INDEX);
    mesh.iBufferInfo_.srcData = mesh.indexData_.data();
}

void NewRenderer::drawFrame(const glm::vec3 eye, const float yaw, const float pitch, float /*roll*/)
{
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) {
        SDL_Log("NewRenderer::drawFrame: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return;
    }

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, window_, &swapchain, &w, &h) || !swapchain) {
        SDL_Log("NewRenderer::drawFrame: SDL_AcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    if (!ensureDepthTexture(w, h)) {
        SDL_Log("NewRenderer::drawFrame: ensureDepthTexture failed");
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    camera_.setEye(eye);
    camera_.setTarget(pitch, yaw, 0.0f);
    camera_.setAspect(static_cast<float>(w), static_cast<float>(h));
    camera_.computeViewProjectionMatrix();

    glm::mat4 view_projection = camera_.getViewProjectionMatrix();

    SDL_PushGPUVertexUniformData(cmd, 0, &view_projection, sizeof(glm::mat4));

    ImDrawData* const k_drawData = ImGui::GetDrawData();
    if (k_drawData)
        ImGui_ImplSDLGPU3_PrepareDrawData(k_drawData, cmd);

    SDL_GPUColorTargetInfo ct{};
    ct.texture = swapchain;
    ct.clear_color = {.r = 0.08f, .g = 0.08f, .b = 0.12f, .a = 1.0f};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo dt{};
    dt.texture = depthTexture;
    dt.clear_depth = 1.0f;
    dt.load_op = SDL_GPU_LOADOP_CLEAR;
    dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.cycle = false;
    dt.clear_stencil = 0;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    SDL_BindGPUGraphicsPipeline(pass, pipeline_);

    SDL_GPUIndexElementSize iElementSizeSdlType = SDL_GPU_INDEXELEMENTSIZE_32BIT;

    for (auto model : Asset::models_) {
        glm::mat4 modelMatrix = glm::mat4(100.0f);
        modelMatrix[3] = glm::vec4(0.0f, 50.0f, 0.0f, 1.0f);
        SDL_PushGPUVertexUniformData(cmd, 1, &modelMatrix, sizeof(glm::mat4));

        Asset::Mesh mesh = Asset::meshes_.at(model.second.meshId_);
        drawMesh(pass, iElementSizeSdlType, mesh);
    }
    //////////////////////////////////////////////////////////////////////////////////
    // std::vector<SDL_GPUBufferBinding> vertexBufferBindings;
    // vertexBufferBindings.push_back(SDL_GPUBufferBinding{.buffer = vBufferInfo_.gpuBuff, .offset = 0});
    // SDL_BindGPUVertexBuffers(pass, 0, vertexBufferBindings.data(), static_cast<Uint32>(vertexBufferBindings.size()));
    //
    // SDL_GPUBufferBinding indexBufferBinding = {.buffer = iBufferInfo_.gpuBuff, .offset = 0};
    // SDL_BindGPUIndexBuffer(pass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    //
    // SDL_DrawGPUIndexedPrimitives(pass, 36, 1, 0, 0, 0);

    //////////////////////////////////////////////////////////////////////////////////

    if (k_drawData)
        ImGui_ImplSDLGPU3_RenderDrawData(k_drawData, cmd, pass);

    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
}

void NewRenderer::drawMesh(SDL_GPURenderPass* renderPass, SDL_GPUIndexElementSize iElementSizeSdlType, Asset::Mesh m)
{
    std::vector<SDL_GPUBufferBinding> vertexBufferBindings;
    vertexBufferBindings.push_back(SDL_GPUBufferBinding{.buffer = m.vBufferInfo_.gpuBuff, .offset = 0});
    SDL_BindGPUVertexBuffers(
        renderPass, 0, vertexBufferBindings.data(), static_cast<Uint32>(vertexBufferBindings.size()));

    SDL_GPUBufferBinding indexBufferBinding = {.buffer = m.iBufferInfo_.gpuBuff, .offset = 0};
    SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, iElementSizeSdlType);

    size_t iElementSizeInt;
    switch (iElementSizeSdlType) {
    case SDL_GPU_INDEXELEMENTSIZE_32BIT:
        iElementSizeInt = 4;
        break;
    case SDL_GPU_INDEXELEMENTSIZE_16BIT:
        iElementSizeInt = 2;
        break;
    default:
        iElementSizeInt = 4;
        break;
    }

    SDL_DrawGPUIndexedPrimitives(renderPass, m.iBufferInfo_.bufferSize / iElementSizeInt, 1, 0, 0, 0);
}

void NewRenderer::uploadDataToGPUBuffer(SDL_GPUCommandBuffer* cmd,
                                        const std::vector<Asset::GeoBufferInfo>& modelBuffersInfo) const
{

    size_t vaoTransferBufferSize = 0;
    for (const auto k_bufferInfo : modelBuffersInfo) {
        vaoTransferBufferSize += k_bufferInfo.bufferSize;
    }
    SDL_GPUTransferBuffer* vaoTransferBuffer = createTransferBuffer(vaoTransferBufferSize, true);
    auto* vaoTransferData = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device_, vaoTransferBuffer, false));

    Uint32 vaoTransferBufferOffset = 0;

    for (const auto k_bufferInfo : modelBuffersInfo) {
        auto* transferBufferData = (vaoTransferData + vaoTransferBufferOffset);
        std::cout << "SDL_memcpy " << std::endl;
        SDL_memcpy(transferBufferData, k_bufferInfo.srcData, k_bufferInfo.bufferSize);
        vaoTransferBufferOffset += k_bufferInfo.bufferSize;
    }

    SDL_UnmapGPUTransferBuffer(device_, vaoTransferBuffer);

    SDL_GPUCopyPass* vaoCopyPass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation vaoTransferBufferLocation = {.transfer_buffer = vaoTransferBuffer, .offset = 0};

    vaoTransferBufferOffset = 0;
    for (const auto k_bufferInfo : modelBuffersInfo) {
        SDL_GPUBufferRegion bufferRegion = {
            .buffer = k_bufferInfo.gpuBuff, .offset = 0, .size = k_bufferInfo.bufferSize};
        SDL_UploadToGPUBuffer(vaoCopyPass, &vaoTransferBufferLocation, &bufferRegion, false);

        vaoTransferBufferLocation.offset += k_bufferInfo.bufferSize;
        // vaoTransferBufferOffset += k_bufferInfo.bufferSize;
    }

    SDL_EndGPUCopyPass(vaoCopyPass);
    SDL_ReleaseGPUTransferBuffer(device_, vaoTransferBuffer);
}

SDL_GPUBuffer* NewRenderer::createGPUBuffer(const size_t bufferSize, const SDL_GPUBufferUsageFlags usage) const
{
    SDL_GPUBufferCreateInfo indexBufferCreateInfo{};
    indexBufferCreateInfo.size = bufferSize;
    indexBufferCreateInfo.usage = usage;

    return SDL_CreateGPUBuffer(device_, &indexBufferCreateInfo);
}

SDL_GPUTransferBuffer* NewRenderer::createTransferBuffer(const size_t transferBufferSize, const bool upload) const
{
    SDL_GPUTransferBufferCreateInfo transferBufferCreateInfo{};
    transferBufferCreateInfo.size = transferBufferSize;
    transferBufferCreateInfo.usage = upload ? SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD : SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;

    return SDL_CreateGPUTransferBuffer(device_, &transferBufferCreateInfo);
}

bool NewRenderer::ensureDepthTexture(Uint32 w, Uint32 h)
{
    if (depthTexture && depthWidth_ == w && depthHeight_ == h)
        return true;

    if (depthTexture) {
        SDL_ReleaseGPUTexture(device_, depthTexture);
        depthTexture = nullptr;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.width = w;
    info.height = h;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;

    depthTexture = SDL_CreateGPUTexture(device_, &info);
    if (!depthTexture) {
        SDL_Log("NewRenderer: failed to create depth texture: %s", SDL_GetError());
        return false;
    }

    depthWidth_ = w;
    depthHeight_ = h;
    return true;
}

void NewRenderer::quit()
{
    if (device_) {
        SDL_WaitForGPUIdle(device_);

        if (depthTexture)
            SDL_ReleaseGPUTexture(device_, depthTexture);

        if (vBufferInfo_.gpuBuff)
            SDL_ReleaseGPUBuffer(device_, vBufferInfo_.gpuBuff);
        if (iBufferInfo_.gpuBuff)
            SDL_ReleaseGPUBuffer(device_, iBufferInfo_.gpuBuff);

        if (pipeline_)
            SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);

        if (ownsDevice_) {
            // Only the device-owner tears down ImGui + the device itself.
            ImGui_ImplSDLGPU3_Shutdown();
            if (ownsWindowClaim_)
                SDL_ReleaseWindowFromGPUDevice(device_, window_);
            SDL_DestroyGPUDevice(device_);
        }
    }

    depthTexture = nullptr;
    pipeline_ = nullptr;
    vBufferInfo_ = {};
    iBufferInfo_ = {};
    device_ = nullptr;
    window_ = nullptr;
    ownsDevice_ = false;
    ownsWindowClaim_ = false;
}
