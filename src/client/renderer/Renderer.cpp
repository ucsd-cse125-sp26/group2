#include "Renderer.hpp"

#include <backends/imgui_impl_sdlgpu3.h>
#include <imgui.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace
{
// Load a compiled shader from disk and create a GPU shader object.
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

// ---------------------------------------------------------------------------
// Renderer implementation
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

    // ---- ImGui GPU backend -------------------------------------------------
    // The ImGui context and SDL3 input backend were already initialised by
    // DebugUI::init(). We just need to hook up the GPU render backend here.
    const SDL_GPUTextureFormat k_colorFmt = SDL_GetGPUSwapchainTextureFormat(device, window);

    ImGui_ImplSDLGPU3_InitInfo imguiInfo{};
    imguiInfo.Device = device;
    imguiInfo.ColorTargetFormat = k_colorFmt;
    imguiInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;

    if (!ImGui_ImplSDLGPU3_Init(&imguiInfo)) {
        SDL_Log("Renderer: ImGui_ImplSDLGPU3_Init failed");
        return false;
    }

    // ---- Scene pipeline (triangle) -----------------------------------------
    const char* const k_base = SDL_GetBasePath();
    const char* const k_ext = (activeFormat == SDL_GPU_SHADERFORMAT_MSL) ? ".msl" : ".spv";

    char vertPath[512], fragPath[512];
    SDL_snprintf(vertPath, sizeof(vertPath), "%sshaders/triangle.vert%s", k_base ? k_base : "", k_ext);
    SDL_snprintf(fragPath, sizeof(fragPath), "%sshaders/triangle.frag%s", k_base ? k_base : "", k_ext);

    SDL_GPUShader* vert = loadShader(device, vertPath, activeFormat, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0, 0);
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
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    pipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);

    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!pipeline) {
        SDL_Log("Renderer: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

void Renderer::drawFrame()
{
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd)
        return;

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, window, &swapchain, &w, &h) || !swapchain) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    // Upload ImGui vertex/index buffers via an internal copy pass.
    // This must happen BEFORE the render pass begins.
    ImDrawData* const k_drawData = ImGui::GetDrawData();
    if (k_drawData)
        ImGui_ImplSDLGPU3_PrepareDrawData(k_drawData, cmd);

    SDL_GPUColorTargetInfo ct{};
    ct.texture = swapchain;
    ct.clear_color = {.r = 0.10f, .g = 0.10f, .b = 0.10f, .a = 1.0f};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);

    // Scene geometry
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

    // ImGui overlay — drawn last so it sits on top of everything
    if (k_drawData)
        ImGui_ImplSDLGPU3_RenderDrawData(k_drawData, cmd, pass);

    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
}

void Renderer::quit()
{
    if (device) {
        SDL_WaitForGPUIdle(device);
        ImGui_ImplSDLGPU3_Shutdown();
        if (pipeline)
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
    }
    pipeline = nullptr;
    device = nullptr;
    window = nullptr;
}
