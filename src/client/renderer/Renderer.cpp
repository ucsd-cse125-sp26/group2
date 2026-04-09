#include "Renderer.hpp"

#include "Camera.hpp"
#include "ModelLoader.hpp"

#include <backends/imgui_impl_sdlgpu3.h>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <imgui.h>
#include <vector>

namespace
{

/// @brief Load a compiled shader from disk and create an SDL GPU shader object.
SDL_GPUShader* loadShader(SDL_GPUDevice* dev,
                          const char* path,
                          SDL_GPUShaderFormat format,
                          SDL_GPUShaderStage stage,
                          Uint32 samplerCount,
                          Uint32 uniformBufferCount,
                          Uint32 storageBufferCount,
                          Uint32 storageTextureCount)
{
    size_t codeSize = 0;
    void* code = SDL_LoadFile(path, &codeSize);
    if (!code) {
        SDL_Log("Renderer: failed to load shader %s: %s", path, SDL_GetError());
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

/// @brief Matrices UBO — shared by both the scene and model pipelines.
struct Matrices
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

} // namespace

// ---------------------------------------------------------------------------
// Renderer::init
// ---------------------------------------------------------------------------

bool Renderer::init(SDL_Window* win)
{
    window = win;

    constexpr SDL_GPUShaderFormat k_wantedFormats = SDL_GPU_SHADERFORMAT_SPIRV
#ifdef HAVE_MSL_SHADERS
                                                    | SDL_GPU_SHADERFORMAT_MSL
#endif
        ;

    device = SDL_CreateGPUDevice(k_wantedFormats, /*debug_mode=*/false, nullptr);
    if (!device) {
        SDL_Log("Renderer: SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    SDL_Log("Renderer: GPU driver = %s", SDL_GetGPUDeviceDriver(device));

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("Renderer: SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    const SDL_GPUShaderFormat k_available = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderFormat activeFormat = SDL_GPU_SHADERFORMAT_INVALID;

    if (k_available & SDL_GPU_SHADERFORMAT_SPIRV)
        activeFormat = SDL_GPU_SHADERFORMAT_SPIRV;
#ifdef HAVE_MSL_SHADERS
    else if (k_available & SDL_GPU_SHADERFORMAT_MSL)
        activeFormat = SDL_GPU_SHADERFORMAT_MSL;
#endif

    if (activeFormat == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_Log("Renderer: no supported shader format (got 0x%x)", static_cast<unsigned>(k_available));
        return false;
    }

    // ImGui GPU backend setup.
    const SDL_GPUTextureFormat k_colorFmt = SDL_GetGPUSwapchainTextureFormat(device, window);

    ImGui_ImplSDLGPU3_InitInfo imguiInfo{};
    imguiInfo.Device = device;
    imguiInfo.ColorTargetFormat = k_colorFmt;
    imguiInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;

    if (!ImGui_ImplSDLGPU3_Init(&imguiInfo)) {
        SDL_Log("Renderer: ImGui_ImplSDLGPU3_Init failed");
        return false;
    }

    // ---- Scene pipeline (cube + floor, hard-coded vertex positions) --------
    const char* const k_base = SDL_GetBasePath();
    const char* const k_ext = (activeFormat == SDL_GPU_SHADERFORMAT_MSL) ? ".msl" : ".spv";

    char vertPath[512], fragPath[512];
    SDL_snprintf(vertPath, sizeof(vertPath), "%sshaders/projective.vert%s", k_base ? k_base : "", k_ext);
    SDL_snprintf(fragPath, sizeof(fragPath), "%sshaders/normal.frag%s", k_base ? k_base : "", k_ext);

    SDL_GPUShader* vert = loadShader(device, vertPath, activeFormat, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 0, 0);
    SDL_GPUShader* frag = loadShader(device, fragPath, activeFormat, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        return false;
    }

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = k_colorFmt;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &colorTarget;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;

    pipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);

    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!pipeline) {
        SDL_Log("Renderer: SDL_CreateGPUGraphicsPipeline (scene) failed: %s", SDL_GetError());
        return false;
    }

    // ---- Model pipeline (vertex-buffer driven, Assimp meshes) -------------
    if (!initModelPipeline(activeFormat, k_colorFmt))
        return false;

    // Load the model from the assets directory next to the binary.
    char modelPath[512];
    SDL_snprintf(modelPath, sizeof(modelPath), "%sassets/Apex_Legend_Wraith.glb", k_base ? k_base : "");

    std::vector<MeshData> meshes;
    if (loadModel(modelPath, meshes)) {
        if (!uploadModel(meshes))
            SDL_Log("Renderer: model GPU upload failed — model will not be drawn");
    } else {
        SDL_Log("Renderer: model load failed — model will not be drawn");
    }

    // Place the model to the right of the reference cube.
    // The cube sits at world (0, 0..64, 368..432).  We park the model at
    // x = +200, y = 0 (ground), z = 400 with a scale of 40 units/metre
    // (assumes the GLB is authored in metres; tweak as needed).
    modelTransform = glm::translate(glm::mat4(1.0f), glm::vec3(200.0f, 0.0f, 400.0f));
    modelTransform = glm::scale(modelTransform, glm::vec3(40.0f));

    // Camera — eye/target overridden every frame by drawFrame().
    camera = Camera(glm::vec3{0.0f, 100.0f, 0.0f},
                    glm::vec3{0.0f, 100.0f, 1.0f},
                    glm::vec3{0.0f, 1.0f, 0.0f},
                    fovyDegrees,
                    1.0f,
                    nearPlane,
                    farPlane);

    return true;
}

// ---------------------------------------------------------------------------
// Renderer::initModelPipeline
// ---------------------------------------------------------------------------

bool Renderer::initModelPipeline(const SDL_GPUShaderFormat fmt, const SDL_GPUTextureFormat colorFmt)
{
    const char* const k_base = SDL_GetBasePath();
    const char* const k_ext = (fmt == SDL_GPU_SHADERFORMAT_MSL) ? ".msl" : ".spv";

    char vertPath[512], fragPath[512];
    SDL_snprintf(vertPath, sizeof(vertPath), "%sshaders/model.vert%s", k_base ? k_base : "", k_ext);
    SDL_snprintf(fragPath, sizeof(fragPath), "%sshaders/model.frag%s", k_base ? k_base : "", k_ext);

    SDL_GPUShader* vert = loadShader(device, vertPath, fmt, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 0, 0);
    SDL_GPUShader* frag = loadShader(device, fragPath, fmt, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        return false;
    }

    // Vertex layout — must match ModelVertex (32 bytes, no padding):
    //   location 0 → position  (vec3, offset  0)
    //   location 1 → normal    (vec3, offset 12)
    //   location 2 → texCoord  (vec2, offset 24)
    const SDL_GPUVertexBufferDescription k_vbDesc = {
        .slot = 0,
        .pitch = sizeof(ModelVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };

    const SDL_GPUVertexAttribute k_attrs[3] = {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0},
        {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 12},
        {.location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 24},
    };

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_buffer_descriptions = &k_vbDesc;
    vertexInput.num_vertex_buffers = 1;
    vertexInput.vertex_attributes = k_attrs;
    vertexInput.num_vertex_attributes = 3;

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = colorFmt;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.vertex_input_state = vertexInput;
    pci.target_info.color_target_descriptions = &colorTarget;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    // Disable back-face culling: GLB winding order varies by exporter.
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    modelPipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);

    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!modelPipeline) {
        SDL_Log("Renderer: SDL_CreateGPUGraphicsPipeline (model) failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Renderer::uploadModel
// ---------------------------------------------------------------------------

bool Renderer::uploadModel(const std::vector<MeshData>& meshes)
{
    if (meshes.empty())
        return false;

    // Collect per-mesh GPU buffer sizes so we can size the transfer buffer.
    struct MeshSizes
    {
        Uint32 vbBytes;
        Uint32 ibBytes;
    };

    std::vector<MeshSizes> sizes;
    sizes.reserve(meshes.size());
    Uint32 totalBytes = 0;

    for (const auto& m : meshes) {
        const Uint32 vb = static_cast<Uint32>(m.vertices.size() * sizeof(ModelVertex));
        const Uint32 ib = static_cast<Uint32>(m.indices.size() * sizeof(uint32_t));
        sizes.push_back({vb, ib});
        totalBytes += vb + ib;
    }

    // One transfer (staging) buffer holds all mesh data.
    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = totalBytes;

    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
    if (!tb) {
        SDL_Log("Renderer: failed to create model transfer buffer: %s", SDL_GetError());
        return false;
    }

    // Map, write all meshes sequentially, then unmap.
    auto* dst = static_cast<char*>(SDL_MapGPUTransferBuffer(device, tb, false));
    Uint32 writeOffset = 0;
    for (size_t i = 0; i < meshes.size(); ++i) {
        SDL_memcpy(dst + writeOffset, meshes[i].vertices.data(), sizes[i].vbBytes);
        writeOffset += sizes[i].vbBytes;
        SDL_memcpy(dst + writeOffset, meshes[i].indices.data(), sizes[i].ibBytes);
        writeOffset += sizes[i].ibBytes;
    }
    SDL_UnmapGPUTransferBuffer(device, tb);

    // Create GPU-resident vertex + index buffers for each mesh, then issue
    // all uploads in a single copy pass.
    SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmd);

    Uint32 readOffset = 0;
    modelMeshes.reserve(meshes.size());

    for (size_t i = 0; i < meshes.size(); ++i) {
        SDL_GPUBufferCreateInfo vbInfo{};
        vbInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        vbInfo.size = sizes[i].vbBytes;
        SDL_GPUBuffer* vb = SDL_CreateGPUBuffer(device, &vbInfo);

        SDL_GPUBufferCreateInfo ibInfo{};
        ibInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        ibInfo.size = sizes[i].ibBytes;
        SDL_GPUBuffer* ib = SDL_CreateGPUBuffer(device, &ibInfo);

        if (!vb || !ib) {
            SDL_Log("Renderer: failed to create GPU buffer for mesh %zu: %s", i, SDL_GetError());
            SDL_ReleaseGPUBuffer(device, vb);
            SDL_ReleaseGPUBuffer(device, ib);
            SDL_EndGPUCopyPass(copyPass);
            SDL_SubmitGPUCommandBuffer(uploadCmd);
            SDL_ReleaseGPUTransferBuffer(device, tb);
            return false;
        }

        SDL_GPUTransferBufferLocation src{};
        SDL_GPUBufferRegion dst2{};

        // Vertex data
        src.transfer_buffer = tb;
        src.offset = readOffset;
        dst2.buffer = vb;
        dst2.offset = 0;
        dst2.size = sizes[i].vbBytes;
        SDL_UploadToGPUBuffer(copyPass, &src, &dst2, false);
        readOffset += sizes[i].vbBytes;

        // Index data
        src.offset = readOffset;
        dst2.buffer = ib;
        dst2.size = sizes[i].ibBytes;
        SDL_UploadToGPUBuffer(copyPass, &src, &dst2, false);
        readOffset += sizes[i].ibBytes;

        modelMeshes.push_back({vb, ib, static_cast<Uint32>(meshes[i].indices.size())});
    }

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(uploadCmd);

    // Wait for the upload to finish before we start rendering.
    SDL_WaitForGPUIdle(device);
    SDL_ReleaseGPUTransferBuffer(device, tb);

    SDL_Log("Renderer: uploaded %zu mesh(es) to GPU (%u bytes total)", modelMeshes.size(), totalBytes);
    return true;
}

// ---------------------------------------------------------------------------
// Renderer::drawFrame
// ---------------------------------------------------------------------------

void Renderer::drawFrame(const glm::vec3 eye, const float yaw, const float pitch)
{
    // ── first-person camera ─────────────────────────────────────────────────
    const float cosPitch = std::cos(pitch);
    const glm::vec3 forward{std::sin(yaw) * cosPitch, -std::sin(pitch), std::cos(yaw) * cosPitch};
    camera.setLookAt(eye, eye + forward, glm::vec3{0.0f, 1.0f, 0.0f});

    // ── GPU frame ───────────────────────────────────────────────────────────
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd)
        return;

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, window, &swapchain, &w, &h) || !swapchain) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    if (!ensureDepthTexture(w, h)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    camera.setAspect((h != 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f);

    // Scene matrices — model = identity (geometry is in world space already).
    Matrices sceneMats{};
    sceneMats.model = glm::mat4(1.0f);
    sceneMats.view = camera.getView();
    sceneMats.projection = camera.getProjection();

    SDL_PushGPUVertexUniformData(cmd, 0, &sceneMats, sizeof(sceneMats));

    // Upload ImGui vertex/index buffers — must happen before the render pass.
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

    // ── Scene geometry: cube (verts 0-35) + floor quad (verts 36-41) ────────
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_DrawGPUPrimitives(pass, 42, 1, 0, 0);

    // ── Assimp model ─────────────────────────────────────────────────────────
    if (!modelMeshes.empty()) {
        // Switch to the model pipeline and push the model-specific transform.
        SDL_BindGPUGraphicsPipeline(pass, modelPipeline);

        Matrices modelMats{};
        modelMats.model = modelTransform;
        modelMats.view = sceneMats.view;
        modelMats.projection = sceneMats.projection;
        SDL_PushGPUVertexUniformData(cmd, 0, &modelMats, sizeof(modelMats));

        for (const auto& mesh : modelMeshes) {
            SDL_GPUBufferBinding vbBind{};
            vbBind.buffer = mesh.vertexBuffer;
            vbBind.offset = 0;
            SDL_BindGPUVertexBuffers(pass, 0, &vbBind, 1);

            SDL_GPUBufferBinding ibBind{};
            ibBind.buffer = mesh.indexBuffer;
            ibBind.offset = 0;
            SDL_BindGPUIndexBuffer(pass, &ibBind, SDL_GPU_INDEXELEMENTSIZE_32BIT);

            SDL_DrawGPUIndexedPrimitives(pass, mesh.indexCount, 1, 0, 0, 0);
        }
    }

    // ── ImGui overlay ────────────────────────────────────────────────────────
    if (k_drawData)
        ImGui_ImplSDLGPU3_RenderDrawData(k_drawData, cmd, pass);

    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
}

// ---------------------------------------------------------------------------
// Renderer::ensureDepthTexture
// ---------------------------------------------------------------------------

bool Renderer::ensureDepthTexture(const Uint32 w, const Uint32 h)
{
    if (depthTexture && depthWidth == w && depthHeight == h)
        return true;

    if (depthTexture) {
        SDL_ReleaseGPUTexture(device, depthTexture);
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

    depthTexture = SDL_CreateGPUTexture(device, &info);
    if (!depthTexture) {
        SDL_Log("Renderer: failed to create depth texture: %s", SDL_GetError());
        return false;
    }

    depthWidth = w;
    depthHeight = h;
    return true;
}

// ---------------------------------------------------------------------------
// Renderer::quit
// ---------------------------------------------------------------------------

void Renderer::quit()
{
    if (device) {
        SDL_WaitForGPUIdle(device);

        // Release model GPU resources.
        for (auto& mesh : modelMeshes) {
            SDL_ReleaseGPUBuffer(device, mesh.vertexBuffer);
            SDL_ReleaseGPUBuffer(device, mesh.indexBuffer);
        }
        modelMeshes.clear();

        if (modelPipeline)
            SDL_ReleaseGPUGraphicsPipeline(device, modelPipeline);

        if (depthTexture)
            SDL_ReleaseGPUTexture(device, depthTexture);

        ImGui_ImplSDLGPU3_Shutdown();

        if (pipeline)
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);

        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
    }

    depthTexture = nullptr;
    pipeline = nullptr;
    modelPipeline = nullptr;
    device = nullptr;
    window = nullptr;
}
