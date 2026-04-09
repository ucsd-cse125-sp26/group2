#include "Renderer.hpp"

#include "Camera.hpp"

#include <backends/imgui_impl_sdlgpu3.h>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <imgui.h>

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

} // namespace

bool Renderer::init(SDL_Window* win)
{
    // Register window
    window = win;

    // Register device
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (!device) {
        SDL_Log("Renderer: SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    SDL_Log("Renderer: GPU driver = %s", SDL_GetGPUDeviceDriver(device));

    // Register window to device
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("Renderer: SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    // Select shader format
    SDL_GPUShaderFormat activeFormat = selectFormat(device);
    if (activeFormat == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_Log("Renderer: no supported shader format (got 0x%x)",
                static_cast<unsigned>(SDL_GetGPUShaderFormats(device)));
        return false;
    }

    // ImGui GPU backend setup.
    // The ImGui context and SDL3 input backend were already initialised by
    // DebugUI::init(). We just hook up the GPU render backend here.
    ImGui_ImplSDLGPU3_InitInfo imguiInfo = createImGuiInfo(device, window);
    if (!ImGui_ImplSDLGPU3_Init(&imguiInfo)) {
        SDL_Log("Renderer: ImGui_ImplSDLGPU3_Init failed");
        return false;
    }

    // Scene pipeline (triangle).
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
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device, window);

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
        SDL_Log("Renderer: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    // Initial camera — eye/target are overridden every frame by drawFrame().
    // near/far must be set correctly here; they persist across setAspect() calls.
    camera = Camera(glm::vec3{0.0f, 100.0f, 0.0f}, // eye  (overridden each frame)
                    glm::vec3{0.0f, 100.0f, 1.0f}, // target
                    glm::vec3{0.0f, 1.0f, 0.0f},   // up
                    fovyDegrees,
                    1.0f,
                    nearPlane,
                    farPlane);

    return true;
}

struct Matrices
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

void Renderer::drawFrame(const glm::vec3 eye, const float yaw, const float pitch)
{
    // ── first-person camera ─────────────────────────────────────────────────
    // Forward vector from yaw (horizontal) and pitch (vertical).
    // Convention matches InputSnapshot: yaw=0 → +Z, pitch>0 → looking down.
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

    // All geometry is pre-positioned in world space, so model = identity.
    Matrices mats{};
    mats.model = glm::mat4(1.0f);
    mats.view = camera.getView();
    mats.projection = camera.getProjection();

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

    // Scene geometry:
    //   verts  0-35  — reference cube (64-unit cube at world pos (0,0,400))
    //   verts 36-41  — floor quad (4000×4000 units at y=0)
    // One draw call; model = identity; checkerboard applied in fragment shader.
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_DrawGPUPrimitives(pass, 42, 1, 0, 0);

    // ImGui overlay — drawn last so it sits on top of scene geometry.
    if (k_drawData)
        ImGui_ImplSDLGPU3_RenderDrawData(k_drawData, cmd, pass);

    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
}

bool Renderer::ensureDepthTexture(Uint32 w, Uint32 h)
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

void Renderer::quit()
{
    if (device) {
        SDL_WaitForGPUIdle(device);

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
    device = nullptr;
    window = nullptr;
}
