#include "Renderer.hpp"

#include "Camera.hpp"

#include <backends/imgui_impl_sdlgpu3.h>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <imgui.h>
#include <iostream>
#include <vector>

#define CUBE_VERTEX_COUNT 24

const float width = 10.0f;
const float width_over_2 = width * 0.5f;

const glm::vec3 cubeMin = -glm::vec3(width_over_2, width_over_2, width_over_2);
const glm::vec3 cubeMax = -cubeMin;

const glm::vec3 positions[CUBE_VERTEX_COUNT] = {
    // Front
    glm::vec3(cubeMin.x, cubeMin.y, cubeMax.z),
    glm::vec3(cubeMax.x, cubeMin.y, cubeMax.z),
    glm::vec3(cubeMax.x, cubeMax.y, cubeMax.z),
    glm::vec3(cubeMin.x, cubeMax.y, cubeMax.z),

    // Back
    glm::vec3(cubeMax.x, cubeMin.y, cubeMin.z),
    glm::vec3(cubeMin.x, cubeMin.y, cubeMin.z),
    glm::vec3(cubeMin.x, cubeMax.y, cubeMin.z),
    glm::vec3(cubeMax.x, cubeMax.y, cubeMin.z),

    // Top
    glm::vec3(cubeMin.x, cubeMax.y, cubeMax.z),
    glm::vec3(cubeMax.x, cubeMax.y, cubeMax.z),
    glm::vec3(cubeMax.x, cubeMax.y, cubeMin.z),
    glm::vec3(cubeMin.x, cubeMax.y, cubeMin.z),

    // Bottom
    glm::vec3(cubeMin.x, cubeMin.y, cubeMin.z),
    glm::vec3(cubeMax.x, cubeMin.y, cubeMin.z),
    glm::vec3(cubeMax.x, cubeMin.y, cubeMax.z),
    glm::vec3(cubeMin.x, cubeMin.y, cubeMax.z),

    // Left
    glm::vec3(cubeMin.x, cubeMin.y, cubeMin.z),
    glm::vec3(cubeMin.x, cubeMin.y, cubeMax.z),
    glm::vec3(cubeMin.x, cubeMax.y, cubeMax.z),
    glm::vec3(cubeMin.x, cubeMax.y, cubeMin.z),

    // Right
    glm::vec3(cubeMax.x, cubeMin.y, cubeMax.z),
    glm::vec3(cubeMax.x, cubeMin.y, cubeMin.z),
    glm::vec3(cubeMax.x, cubeMax.y, cubeMin.z),
    glm::vec3(cubeMax.x, cubeMax.y, cubeMax.z)};

// Specify normals
const glm::vec3 normals[CUBE_VERTEX_COUNT] = {
    // Front
    glm::vec3(0, 0, 1),
    glm::vec3(0, 0, 1),
    glm::vec3(0, 0, 1),
    glm::vec3(0, 0, 1),

    // Back
    glm::vec3(0, 0, -1),
    glm::vec3(0, 0, -1),
    glm::vec3(0, 0, -1),
    glm::vec3(0, 0, -1),

    // Top
    glm::vec3(0, 1, 0),
    glm::vec3(0, 1, 0),
    glm::vec3(0, 1, 0),
    glm::vec3(0, 1, 0),

    // Bottom
    glm::vec3(0, -1, 0),
    glm::vec3(0, -1, 0),
    glm::vec3(0, -1, 0),
    glm::vec3(0, -1, 0),

    // Left
    glm::vec3(-1, 0, 0),
    glm::vec3(-1, 0, 0),
    glm::vec3(-1, 0, 0),
    glm::vec3(-1, 0, 0),

    // Right
    glm::vec3(1, 0, 0),
    glm::vec3(1, 0, 0),
    glm::vec3(1, 0, 0),
    glm::vec3(1, 0, 0)};

static Vertex cubeVertexData[CUBE_VERTEX_COUNT];

// Specify indices
const Uint32 indices[36] = {
    0,  1,  2,  0,  2,  3,  // Front
    4,  5,  6,  4,  6,  7,  // Back
    8,  9,  10, 8,  10, 11, // Top
    12, 13, 14, 12, 14, 15, // Bottom
    16, 17, 18, 16, 18, 19, // Left
    20, 21, 22, 20, 22, 23  // Right
};

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

/// @brief Load a compiled shader from disk and create an SDL GPU shader object.
/// @param dev                  The GPU device.
/// @param path                 Path to the compiled shader file (.spv or .msl).
/// @param format               Shader format (SPIR-V or MSL).
/// @param stage                Vertex or fragment stage.
/// @param samplerCount         Number of texture samplers declared in the shader.
/// @param uniformBufferCount   Number of uniform buffers declared in the shader.
/// @param storageBufferCount   Number of storage buffers declared in the shader.
/// @param storageTextureCount  Number of storage textures declared in the shader.
/// @return The created shader, or nullptr on failure (error logged via SDL_Log).
SDL_GPUShader* loadShader(SDL_GPUDevice* dev,
                          const char* path,
                          SDL_GPUShaderFormat format,
                          SDL_GPUShaderStage stage,
                          Uint32 samplerCount,
                          Uint32 uniformBufferCount,
                          Uint32 storageBufferCount,
                          Uint32 storageTextureCount)
{
    const char* const k_base = SDL_GetBasePath();
    const char* const k_ext = (format == SDL_GPU_SHADERFORMAT_MSL) ? ".msl" : ".spv";

    char fullPath[512];
    SDL_snprintf(fullPath, sizeof(fullPath), "%s%s%s", k_base ? k_base : "", path, k_ext);

    size_t codeSize = 0;
    void* code = SDL_LoadFile(fullPath, &codeSize);
    if (!code) {
        SDL_Log("Renderer: failed to load shader %s: %s", fullPath, SDL_GetError());
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info{};
    info.code = static_cast<const Uint8*>(code);
    info.code_size = static_cast<Uint32>(codeSize);
    info.format = format;
    info.stage = stage;
    info.num_samplers = samplerCount;
    info.num_uniform_buffers = uniformBufferCount;
    info.num_storage_buffers = storageBufferCount;
    info.num_storage_textures = storageTextureCount;

    // SPIR-V entry point is "main"; spirv-cross renames it to "main0" in MSL
    // (Metal forbids a function literally named "main").
    info.entrypoint = (format == SDL_GPU_SHADERFORMAT_MSL) ? "main0" : "main";

    SDL_GPUShader* shader = SDL_CreateGPUShader(dev, &info);
    SDL_free(code);

    if (!shader)
        SDL_Log("Renderer: SDL_CreateGPUShader(%s) failed: %s", path, SDL_GetError());
    return shader;
}

SDL_GPUGraphicsPipeline*
createGeometryPipeline(SDL_GPUDevice* device, SDL_Window* window, SDL_GPUShaderFormat shaderFormat)
{
    // --- Geometry Shaders
    // --- If you change the shader names/locations or
    // --- buffer/texture counts, then update them here
    // Vertex Shader
    const char* const vertexShaderPath = "shaders/geometry.vert";
    Uint32 vertexShaderSamplerCount = 0;
    Uint32 vertexShaderUniformBufferCount = 1;
    Uint32 vertexShaderStorageBufferCount = 0;
    Uint32 vertexShaderStorageTextureCount = 0;
    // Fragment Shader
    const char* const fragmentShaderPath = "shaders/geometry.frag";
    Uint32 fragmentShaderSamplerCount = 0;
    Uint32 fragmentShaderUniformBufferCount = 0;
    Uint32 fragmentShaderStorageBufferCount = 0;
    Uint32 fragmentShaderStorageTextureCount = 0;
    // ---

    SDL_GPUShader* vertexShader = loadShader(device,
                                             vertexShaderPath,
                                             shaderFormat,
                                             SDL_GPU_SHADERSTAGE_VERTEX,
                                             vertexShaderSamplerCount,
                                             vertexShaderUniformBufferCount,
                                             vertexShaderStorageBufferCount,
                                             vertexShaderStorageTextureCount);
    SDL_GPUShader* fragmentShader = loadShader(device,
                                               fragmentShaderPath,
                                               shaderFormat,
                                               SDL_GPU_SHADERSTAGE_FRAGMENT,
                                               fragmentShaderSamplerCount,
                                               fragmentShaderUniformBufferCount,
                                               fragmentShaderStorageBufferCount,
                                               fragmentShaderStorageTextureCount);

    if (!vertexShader || !fragmentShader) {
        SDL_ReleaseGPUShader(device, vertexShader);
        SDL_ReleaseGPUShader(device, fragmentShader);
        return nullptr; // false
    }

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device, window);

    //////////////////////////////////////////////////////////// VERTEX_INPUT_STATE: INTERLEAVED POS/NORM/UV
    ///////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////// VERTEX DESCRIPTIONS
    std::vector<SDL_GPUVertexBufferDescription> vertexBufferDescriptions;

    SDL_GPUVertexBufferDescription vBufferDescrFullInterleaved;
    vBufferDescrFullInterleaved.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vBufferDescrFullInterleaved.instance_step_rate = 0;
    vBufferDescrFullInterleaved.pitch = sizeof(Vertex);
    vBufferDescrFullInterleaved.slot = 0;

    vertexBufferDescriptions.push_back(vBufferDescrFullInterleaved);

    //////////////////////////////////////////////////////////// VERTEX ATTRIBUTES
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

    //////////////////////////////////////////////////////////// PIPELINE CREATE INFO
    ///////////////////////////////////////////////////////////////

    SDL_GPUGraphicsPipelineCreateInfo pci{}; // Missing .multisample_state
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
    // pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);

    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);

    return pipeline;
}

} // namespace

bool Renderer::init(SDL_Window* win)
{
    // Register window
    window_ = win;

    // Register device
    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (!device_) {
        SDL_Log("Renderer: SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    SDL_Log("Renderer: GPU driver = %s", SDL_GetGPUDeviceDriver(device_));

    // Register window to device
    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
        SDL_Log("Renderer: SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    // Select shader format
    SDL_GPUShaderFormat activeFormat = selectFormat(device_);
    if (activeFormat == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_Log("Renderer: no supported shader format (got 0x%x)",
                static_cast<unsigned>(SDL_GetGPUShaderFormats(device_)));
        return false;
    }

    // ImGui GPU backend setup.
    // The ImGui context and SDL3 input backend were already initialised by
    // DebugUI::init(). We just hook up the GPU render backend here.
    ImGui_ImplSDLGPU3_InitInfo imguiInfo = createImGuiInfo(device_, window_);
    if (!ImGui_ImplSDLGPU3_Init(&imguiInfo)) {
        SDL_Log("Renderer: ImGui_ImplSDLGPU3_Init failed");
        return false;
    }

    // Geometry pipeline
    pipeline_ = createGeometryPipeline(device_, window_, activeFormat);
    if (!pipeline_) {
        SDL_Log("Renderer: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    // Initial camera — eye/target are overridden every frame by drawFrame().
    // near/far must be set correctly here; they persist across setAspect() calls.
    camera_ = Camera(glm::vec3{0.0f, 0.0f, 20.0f},  // eye  (overridden each frame)
                     glm::vec3{0.0f, 100.0f, 1.0f}, // target
                     glm::vec3{0.0f, 1.0f, 0.0f},   // up
                     fovyDegrees_,
                     1.0f,
                     nearPlane_,
                     farPlane_);

    for (int i = 0; i < CUBE_VERTEX_COUNT; i++) {
        constexpr auto k_uvNot = glm::vec2(0.0f);
        Vertex vi{};
        vi.position = positions[i];
        vi.normal = normals[i];
        vi.texUV = k_uvNot;
        cubeVertexData[i] = vi;
    }

    //////////////////////////////////////////////////////////////////// CREATE VAO BUFFERS
    //////////////////////////////////////////////////////////////////////////
    Uint32 numVertices = sizeof(cubeVertexData) / sizeof(Vertex);
    Uint32 numIndices = sizeof(indices) / sizeof(Uint32);

    vBufferInfo_.bufferSize = numVertices * sizeof(Vertex);
    vBufferInfo_.gpuBuff = createGPUBuffer(vBufferInfo_.bufferSize, SDL_GPU_BUFFERUSAGE_VERTEX);
    vBufferInfo_.srcData = nullptr;

    iBufferInfo_.bufferSize = numIndices * sizeof(Uint32);
    iBufferInfo_.gpuBuff = createGPUBuffer(iBufferInfo_.bufferSize, SDL_GPU_BUFFERUSAGE_INDEX);
    iBufferInfo_.srcData = nullptr;
    //////////////////////////////////////////////////////////////////// CREATE VAO BUFFERS
    //////////////////////////////////////////////////////////////////////////

    std::cout << "copy vao data to new transfer buffer" << std::endl;
    //////////////////////////////////////////////////////////////////// COPY VAO DATA TO NEW TRANSFERBUFFER
    //////////////////////////////////////////////////////////////////////////
    size_t vaoTransferBufferSize = vBufferInfo_.bufferSize + iBufferInfo_.bufferSize;
    SDL_GPUTransferBuffer* vaoTransferBuffer = createTransferBuffer(vaoTransferBufferSize, true);

    auto* vaoTransferData = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device_, vaoTransferBuffer, false));
    auto* vertexTransferBufferData = (vaoTransferData);
    auto* indexTransferBufferData = (vaoTransferData + vBufferInfo_.bufferSize);

    SDL_memcpy(vertexTransferBufferData, cubeVertexData, vBufferInfo_.bufferSize);
    SDL_memcpy(indexTransferBufferData, indices, iBufferInfo_.bufferSize);

    SDL_UnmapGPUTransferBuffer(device_, vaoTransferBuffer);
    //////////////////////////////////////////////////////////////////// COPY VAO DATA TO NEW TRANSFERBUFFER
    //////////////////////////////////////////////////////////////////////////

    std::cout << "upload transfer buffer to gpu buffer" << std::endl;
    //////////////////////////////////////////////////////////////////// UPLOAD TRANSFERBUFFER DATA TO GPU BUFFER
    //////////////////////////////////////////////////////////////////////////
    SDL_GPUCommandBuffer* cmdCopyBuff = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* vaoCopyPass = SDL_BeginGPUCopyPass(cmdCopyBuff);

    std::cout << "upload vertexBuffer Data" << std::endl;
    SDL_GPUTransferBufferLocation vertexTransferBufferLocation = {.transfer_buffer = vaoTransferBuffer, .offset = 0};
    SDL_GPUBufferRegion vertexBufferRegion = {
        .buffer = vBufferInfo_.gpuBuff, .offset = 0, .size = vBufferInfo_.bufferSize};
    SDL_UploadToGPUBuffer(vaoCopyPass, &vertexTransferBufferLocation, &vertexBufferRegion, false);

    std::cout << "upload indexBuffer Data" << std::endl;
    SDL_GPUTransferBufferLocation indexTransferBufferLocation = {.transfer_buffer = vaoTransferBuffer,
                                                                 .offset = vBufferInfo_.bufferSize};
    SDL_GPUBufferRegion indexBufferRegion = {
        .buffer = iBufferInfo_.gpuBuff, .offset = 0, .size = iBufferInfo_.bufferSize};
    SDL_UploadToGPUBuffer(vaoCopyPass, &indexTransferBufferLocation, &indexBufferRegion, false);

    std::cout << "end GPU copy pass" << std::endl;
    SDL_EndGPUCopyPass(vaoCopyPass);
    std::cout << "release Transfer buffer" << std::endl;
    SDL_ReleaseGPUTransferBuffer(device_, vaoTransferBuffer);
    //////////////////////////////////////////////////////////////////// UPLOAD TRANSFERBUFFER DATA TO GPU BUFFER
    //////////////////////////////////////////////////////////////////////////
    SDL_SubmitGPUCommandBuffer(cmdCopyBuff);

    return true;
}

void Renderer::drawFrame(const glm::vec3 eye, const float yaw, const float pitch)
{

    // ── first-person camera ─────────────────────────────────────────────────
    // Forward vector from yaw (horizontal) and pitch (vertical).
    // Convention matches InputSnapshot: yaw=0 → +Z, pitch>0 → looking down.
    const float cosPitch = std::cos(pitch);
    const glm::vec3 forward{std::sin(yaw) * cosPitch, -std::sin(pitch), std::cos(yaw) * cosPitch};
    camera_.setLookAt(eye, eye + forward, glm::vec3{0.0f, 1.0f, 0.0f});

    // ── GPU frame ───────────────────────────────────────────────────────────
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) {
        return;
    }

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, window_, &swapchain, &w, &h) || !swapchain) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    if (!ensureDepthTexture(w, h)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    camera_.setAspect((h != 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f);

    // All geometry is pre-positioned in world space, so model = identity.
    Matrices mats{};
    mats.model = glm::mat4(1.0f);
    mats.model[3] = glm::vec4(0.0f, 100.0f, 0.0f, 1.0f);
    mats.view = camera_.getView();
    mats.projection = camera_.getProjection();

    SDL_PushGPUVertexUniformData(cmd, 0, &mats, sizeof(mats));

    // Upload ImGui vertex/index buffers via an internal copy pass.
    // This must happen BEFORE the render pass begins.
    ImDrawData* const k_drawData = ImGui::GetDrawData();
    if (k_drawData)
        ImGui_ImplSDLGPU3_PrepareDrawData(k_drawData, cmd);

    SDL_GPUColorTargetInfo ct{};
    ct.texture = swapchain;
    ct.clear_color = {.r = 0.08f, .g = 0.08f, .b = 0.12f, .a = 1.0f}; // dark-blue sky
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
    /////////////////////////////////////////////////////////////////// BIND GPUBUFFERBINDINGS TO RENDERPASS
    //////////////////////////////////////////////////////////////////////////
    std::vector<SDL_GPUBufferBinding> vertexBufferBindings;

    vertexBufferBindings.push_back(SDL_GPUBufferBinding{.buffer = vBufferInfo_.gpuBuff, .offset = 0});
    SDL_BindGPUVertexBuffers(pass, 0, vertexBufferBindings.data(), static_cast<Uint32>(vertexBufferBindings.size()));

    SDL_GPUBufferBinding indexBufferBinding = {.buffer = iBufferInfo_.gpuBuff, .offset = 0};
    SDL_BindGPUIndexBuffer(pass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    /////////////////////////////////////////////////////////////////// BIND GPUBUFFERBINDINGS TO RENDERPASS
    //////////////////////////////////////////////////////////////////////////

    SDL_DrawGPUIndexedPrimitives(pass, 36, 1, 0, 0, 0);
    // ImGui overlay — drawn last so it sits on top of scene geometry.
    if (k_drawData)
        ImGui_ImplSDLGPU3_RenderDrawData(k_drawData, cmd, pass);

    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmd);
    return;
}

void Renderer::uploadDataToGPUBuffer(SDL_GPUCommandBuffer* cmd,
                                     const std::vector<ModelBufferInfo>& modelBuffersInfo) const
{
    ///////////////////////////////// TRANSFERBUFFER SETUP
    size_t vaoTransferBufferSize = 0;
    for (const auto k_bufferInfo : modelBuffersInfo) {
        vaoTransferBufferSize += k_bufferInfo.bufferSize;
    }
    SDL_GPUTransferBuffer* vaoTransferBuffer = createTransferBuffer(vaoTransferBufferSize, true);
    auto* vaoTransferData = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device_, vaoTransferBuffer, false));

    ///////////////////////////////// COPY DATA TO TRANSFERBUFFER
    Uint32 vaoTransferBufferOffset = 0;
    for (const auto k_bufferInfo : modelBuffersInfo) {
        auto* transferBufferData = (vaoTransferData + vaoTransferBufferOffset);
        SDL_memcpy(transferBufferData, cubeVertexData, k_bufferInfo.bufferSize);

        vaoTransferBufferOffset += k_bufferInfo.bufferSize;
    }

    SDL_UnmapGPUTransferBuffer(device_, vaoTransferBuffer);

    ///////////////////////////////// UPLOAD TRANSFERBUFFER TO GPU
    SDL_GPUCopyPass* vaoCopyPass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation vaoTransferBufferLocation = {.transfer_buffer = vaoTransferBuffer, .offset = 0};

    vaoTransferBufferOffset = 0;
    for (const auto k_bufferInfo : modelBuffersInfo) {
        SDL_GPUBufferRegion bufferRegion = {
            .buffer = k_bufferInfo.gpuBuff, .offset = vaoTransferBufferOffset, .size = k_bufferInfo.bufferSize};
        SDL_UploadToGPUBuffer(vaoCopyPass, &vaoTransferBufferLocation, &bufferRegion, false);

        vaoTransferBufferOffset += k_bufferInfo.bufferSize;
    }

    SDL_EndGPUCopyPass(vaoCopyPass);
    SDL_ReleaseGPUTransferBuffer(device_, vaoTransferBuffer);
}

SDL_GPUBuffer* Renderer::createGPUBuffer(const size_t bufferSize, const SDL_GPUBufferUsageFlags usage) const
{
    SDL_GPUBufferCreateInfo indexBufferCreateInfo{};
    indexBufferCreateInfo.size = bufferSize;
    indexBufferCreateInfo.usage = usage;

    return SDL_CreateGPUBuffer(device_, &indexBufferCreateInfo);
};

SDL_GPUTransferBuffer* Renderer::createTransferBuffer(const size_t transferBufferSize, const bool upload) const
{
    SDL_GPUTransferBufferCreateInfo transferBufferCreateInfo{};
    transferBufferCreateInfo.size = transferBufferSize;
    transferBufferCreateInfo.usage = upload ? SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD : SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;

    return SDL_CreateGPUTransferBuffer(device_, &transferBufferCreateInfo);
};

bool Renderer::ensureDepthTexture(Uint32 w, Uint32 h)
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
        SDL_Log("Renderer: failed to create depth texture: %s", SDL_GetError());
        return false;
    }

    depthWidth_ = w;
    depthHeight_ = h;
    return true;
}

void Renderer::quit()
{
    if (device_) {
        SDL_WaitForGPUIdle(device_);

        if (depthTexture)
            SDL_ReleaseGPUTexture(device_, depthTexture);

        ImGui_ImplSDLGPU3_Shutdown();

        if (pipeline_)
            SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);

        SDL_ReleaseWindowFromGPUDevice(device_, window_);
        SDL_DestroyGPUDevice(device_);
    }

    depthTexture = nullptr;
    pipeline_ = nullptr;
    device_ = nullptr;
    window_ = nullptr;
}
