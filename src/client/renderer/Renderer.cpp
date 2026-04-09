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

    // SPIR-V entry point is "main"; spirv-cross renames it to "main0" in MSL.
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

    // ---- Model pipeline (vertex-buffer driven, Assimp meshes + textures) --
    if (!initModelPipeline(activeFormat, k_colorFmt))
        return false;

    // Load the model from the assets directory next to the binary.
    char modelPath[512];
    SDL_snprintf(modelPath, sizeof(modelPath), "%sassets/Apex_Legend_Wraith.glb", k_base ? k_base : "");

    LoadedModel loadedModel;
    if (loadModel(modelPath, loadedModel)) {
        if (!uploadModel(loadedModel))
            SDL_Log("Renderer: model GPU upload failed — model will not be drawn");
    } else {
        SDL_Log("Renderer: model load failed — model will not be drawn");
    }

    // Place the model to the right of the reference cube.
    // Cube sits at world (0, 0..64, 368..432).  Model goes at
    // x=+200, y=0 (ground), z=400.  Scale of 8 units/metre gives ~14 units
    // per foot — plausible for a Quake-unit world (tweak as needed).
    modelTransform = glm::translate(glm::mat4(1.0f), glm::vec3(200.0f, 0.0f, 400.0f));
    modelTransform = glm::scale(modelTransform, glm::vec3(8.0f));

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

    // Vertex shader: 0 samplers, 1 UBO.
    // Fragment shader: 1 sampler (texDiffuse), 0 UBOs.
    SDL_GPUShader* vert = loadShader(device, vertPath, fmt, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 0, 0);
    SDL_GPUShader* frag = loadShader(device, fragPath, fmt, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0, 0, 0);
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
    // GLB materials are double-sided; disable culling to match.
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
// Renderer::uploadTexture
// ---------------------------------------------------------------------------

SDL_GPUTexture* Renderer::uploadTexture(const uint8_t* pixels, const int width, const int height)
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &info);
    if (!tex) {
        SDL_Log("Renderer: SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return nullptr;
    }

    const Uint32 dataSize = static_cast<Uint32>(width) * static_cast<Uint32>(height) * 4u;

    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = dataSize;

    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
    if (!tb) {
        SDL_Log("Renderer: failed to create texture transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUTexture(device, tex);
        return nullptr;
    }

    void* ptr = SDL_MapGPUTransferBuffer(device, tb, false);
    SDL_memcpy(ptr, pixels, dataSize);
    SDL_UnmapGPUTransferBuffer(device, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.offset = 0;
    src.pixels_per_row = 0; // 0 = tightly packed (width pixels per row)
    src.rows_per_layer = 0; // 0 = tightly packed (height rows per layer)

    SDL_GPUTextureRegion dst{};
    dst.texture = tex;
    dst.mip_level = 0;
    dst.layer = 0;
    dst.x = dst.y = dst.z = 0;
    dst.w = static_cast<Uint32>(width);
    dst.h = static_cast<Uint32>(height);
    dst.d = 1;

    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(device);
    SDL_ReleaseGPUTransferBuffer(device, tb);

    return tex;
}

// ---------------------------------------------------------------------------
// Renderer::uploadModel
// ---------------------------------------------------------------------------

bool Renderer::uploadModel(const LoadedModel& model)
{
    if (model.meshes.empty())
        return false;

    // ---- Create the shared sampler -------------------------------------------
    SDL_GPUSamplerCreateInfo sampInfo{};
    sampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    sampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampInfo.min_lod = 0.0f;
    sampInfo.max_lod = 0.0f;
    sampInfo.enable_anisotropy = false;
    sampInfo.enable_compare = false;

    modelSampler = SDL_CreateGPUSampler(device, &sampInfo);
    if (!modelSampler) {
        SDL_Log("Renderer: failed to create model sampler: %s", SDL_GetError());
        return false;
    }

    // ---- 1×1 opaque-white fallback texture -----------------------------------
    const uint8_t k_white[4] = {255, 255, 255, 255};
    defaultTexture = uploadTexture(k_white, 1, 1);
    if (!defaultTexture)
        return false;

    // ---- Upload each model texture -------------------------------------------
    modelTextures.reserve(model.textures.size());
    for (const auto& td : model.textures) {
        SDL_GPUTexture* gpuTex = uploadTexture(td.pixels.data(), td.width, td.height);
        modelTextures.push_back(gpuTex); // nullptr means "use default" at draw time
    }

    // ---- Collect per-mesh vertex/index sizes then upload all in one pass ----
    struct MeshSizes
    {
        Uint32 vbBytes;
        Uint32 ibBytes;
    };

    std::vector<MeshSizes> sizes;
    sizes.reserve(model.meshes.size());
    Uint32 totalBytes = 0;

    for (const auto& m : model.meshes) {
        const Uint32 vb = static_cast<Uint32>(m.vertices.size() * sizeof(ModelVertex));
        const Uint32 ib = static_cast<Uint32>(m.indices.size() * sizeof(uint32_t));
        sizes.push_back({.vbBytes = vb, .ibBytes = ib});
        totalBytes += vb + ib;
    }

    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = totalBytes;

    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
    if (!tb) {
        SDL_Log("Renderer: failed to create model transfer buffer: %s", SDL_GetError());
        return false;
    }

    auto* dst = static_cast<char*>(SDL_MapGPUTransferBuffer(device, tb, false));
    Uint32 writeOffset = 0;
    for (size_t i = 0; i < model.meshes.size(); ++i) {
        SDL_memcpy(dst + writeOffset, model.meshes[i].vertices.data(), sizes[i].vbBytes);
        writeOffset += sizes[i].vbBytes;
        SDL_memcpy(dst + writeOffset, model.meshes[i].indices.data(), sizes[i].ibBytes);
        writeOffset += sizes[i].ibBytes;
    }
    SDL_UnmapGPUTransferBuffer(device, tb);

    SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmd);

    Uint32 readOffset = 0;
    modelMeshes.reserve(model.meshes.size());

    for (size_t i = 0; i < model.meshes.size(); ++i) {
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
        SDL_GPUBufferRegion dstReg{};

        src.transfer_buffer = tb;
        src.offset = readOffset;
        dstReg.buffer = vb;
        dstReg.offset = 0;
        dstReg.size = sizes[i].vbBytes;
        SDL_UploadToGPUBuffer(copyPass, &src, &dstReg, false);
        readOffset += sizes[i].vbBytes;

        src.offset = readOffset;
        dstReg.buffer = ib;
        dstReg.size = sizes[i].ibBytes;
        SDL_UploadToGPUBuffer(copyPass, &src, &dstReg, false);
        readOffset += sizes[i].ibBytes;

        modelMeshes.push_back({
            .vertexBuffer = vb,
            .indexBuffer = ib,
            .indexCount = static_cast<Uint32>(model.meshes[i].indices.size()),
            .textureIndex = model.meshes[i].diffuseTexIndex,
        });
    }

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(uploadCmd);
    SDL_WaitForGPUIdle(device);
    SDL_ReleaseGPUTransferBuffer(device, tb);

    SDL_Log("Renderer: uploaded %zu mesh(es), %zu texture(s) to GPU (%u bytes geometry)",
            modelMeshes.size(),
            modelTextures.size(),
            totalBytes);
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

    // Scene matrices — model = identity (geometry is already in world space).
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

    // ── Scene geometry: cube (verts 0–35) + floor quad (verts 36–41) ────────
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_DrawGPUPrimitives(pass, 42, 1, 0, 0);

    // ── Assimp model ─────────────────────────────────────────────────────────
    if (!modelMeshes.empty() && modelPipeline && modelSampler && defaultTexture) {
        SDL_BindGPUGraphicsPipeline(pass, modelPipeline);

        Matrices modelMats{};
        modelMats.model = modelTransform;
        modelMats.view = sceneMats.view;
        modelMats.projection = sceneMats.projection;
        SDL_PushGPUVertexUniformData(cmd, 0, &modelMats, sizeof(modelMats));

        for (const auto& mesh : modelMeshes) {
            // Vertex + index buffers
            const SDL_GPUBufferBinding vbBind = {.buffer = mesh.vertexBuffer, .offset = 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vbBind, 1);

            const SDL_GPUBufferBinding ibBind = {.buffer = mesh.indexBuffer, .offset = 0};
            SDL_BindGPUIndexBuffer(pass, &ibBind, SDL_GPU_INDEXELEMENTSIZE_32BIT);

            // Base-colour texture (fall back to 1×1 white if none loaded)
            SDL_GPUTexture* tex = defaultTexture;
            if (mesh.textureIndex >= 0 && static_cast<size_t>(mesh.textureIndex) < modelTextures.size() &&
                modelTextures[static_cast<size_t>(mesh.textureIndex)] != nullptr)
            {
                tex = modelTextures[static_cast<size_t>(mesh.textureIndex)];
            }

            const SDL_GPUTextureSamplerBinding tsb = {.texture = tex, .sampler = modelSampler};
            SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);

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

        for (auto* tex : modelTextures)
            SDL_ReleaseGPUTexture(device, tex);
        modelTextures.clear();

        if (defaultTexture)
            SDL_ReleaseGPUTexture(device, defaultTexture);

        if (modelSampler)
            SDL_ReleaseGPUSampler(device, modelSampler);

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

    defaultTexture = nullptr;
    modelSampler = nullptr;
    modelPipeline = nullptr;
    depthTexture = nullptr;
    pipeline = nullptr;
    device = nullptr;
    window = nullptr;
}
