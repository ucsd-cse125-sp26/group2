#include "Renderer.hpp"

#include "Camera.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
// Load a shader from disk.
// path   — full path to the .spv or .msl file
// stage  — vertex or fragment
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

    // Advertise every shader format we have compiled shaders for.
    // SDL3 picks the best available backend (Vulkan on Linux/Windows → SPIR-V,
    // Metal on macOS → MSL).
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

    // Determine which single format the chosen backend actually uses.
    const SDL_GPUShaderFormat k_available = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderFormat activeFormat = SDL_GPU_SHADERFORMAT_INVALID;

    if (k_available & SDL_GPU_SHADERFORMAT_SPIRV) {
        activeFormat = SDL_GPU_SHADERFORMAT_SPIRV;
    }
#ifdef HAVE_MSL_SHADERS
    else if (k_available & SDL_GPU_SHADERFORMAT_MSL)
    {
        activeFormat = SDL_GPU_SHADERFORMAT_MSL;
    }
#endif

    if (activeFormat == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_Log("Renderer: no supported shader format available (got 0x%x)", static_cast<unsigned>(k_available));
        return false;
    }

    // Build paths to compiled shaders next to the binary.
    const char* base = SDL_GetBasePath();
    const char* ext = (activeFormat == SDL_GPU_SHADERFORMAT_MSL) ? ".msl" : ".spv";
    char vertPath[512], fragPath[512];
    SDL_snprintf(vertPath, sizeof(vertPath), "%sshaders/projective.vert%s", base ? base : "", ext);
    SDL_snprintf(fragPath, sizeof(fragPath), "%sshaders/normal.frag%s", base ? base : "", ext);

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

    // Shaders are baked into the pipeline — release our references.
    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!pipeline) {
        SDL_Log("Renderer: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    camera = Camera(eye,
                    target,
                    up,
                    glm::degrees(fovy), // only if your fovy is currently radians
                    1.0f,
                    near,
                    far);

    return true;
}

struct Matrices
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

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

    if (!ensureDepthTexture(w, h)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    float aspect = (h != 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;

    Matrices mats{};
    mats.model = glm::mat4(1.0f);
    camera.setAspect(aspect);
    mats.view = camera.getView();
    mats.projection = camera.getProjection();

    SDL_PushGPUVertexUniformData(cmd, 0, &mats, sizeof(mats));

    SDL_GPUColorTargetInfo ct{};
    ct.texture = swapchain;
    ct.clear_color = {.r = 0.10f, .g = 0.10f, .b = 0.10f, .a = 1.0f};
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
    // dt.mip_level = 0;
    // dt.layer = 0;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_DrawGPUPrimitives(pass, 36, 1, 0, 0);
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
