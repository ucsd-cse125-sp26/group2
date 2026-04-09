#include "Renderer.hpp"

#include "Camera.hpp"
#include "ShaderUtils.hpp"
#include "particles/ParticleSystem.hpp"

#include <backends/imgui_impl_sdlgpu3.h>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <imgui.h>

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
    shaderFormat = SDL_GPU_SHADERFORMAT_INVALID;

    if (k_available & SDL_GPU_SHADERFORMAT_SPIRV)
        shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
#ifdef HAVE_MSL_SHADERS
    else if (k_available & SDL_GPU_SHADERFORMAT_MSL)
        shaderFormat = SDL_GPU_SHADERFORMAT_MSL;
#endif

    if (shaderFormat == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_Log("Renderer: no supported shader format (got 0x%x)", static_cast<unsigned>(k_available));
        return false;
    }

    // ImGui GPU backend setup.
    colorFormat = SDL_GetGPUSwapchainTextureFormat(device, window);

    ImGui_ImplSDLGPU3_InitInfo imguiInfo{};
    imguiInfo.Device = device;
    imguiInfo.ColorTargetFormat = colorFormat;
    imguiInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;

    if (!ImGui_ImplSDLGPU3_Init(&imguiInfo)) {
        SDL_Log("Renderer: ImGui_ImplSDLGPU3_Init failed");
        return false;
    }

    // Scene pipeline (projective.vert + normal.frag).
    const char* const k_base = SDL_GetBasePath();
    const char* const k_ext = (shaderFormat == SDL_GPU_SHADERFORMAT_MSL) ? ".msl" : ".spv";

    char vertPath[512], fragPath[512];
    SDL_snprintf(vertPath, sizeof(vertPath), "%sshaders/projective.vert%s", k_base ? k_base : "", k_ext);
    SDL_snprintf(fragPath, sizeof(fragPath), "%sshaders/normal.frag%s", k_base ? k_base : "", k_ext);

    SDL_GPUShader* vert = loadShader(device, vertPath, shaderFormat, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, 0, 0);
    SDL_GPUShader* frag = loadShader(device, fragPath, shaderFormat, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        return false;
    }

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = colorFormat;

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
    camera = Camera(glm::vec3{0.0f, 100.0f, 0.0f},
                    glm::vec3{0.0f, 100.0f, 1.0f},
                    glm::vec3{0.0f, 1.0f, 0.0f},
                    fovyDegrees,
                    1.0f,
                    nearPlane,
                    farPlane);

    return true;
}

// Uniform block layout shared with projective.vert (set=1, binding=0).
struct Matrices
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

// Uniform block layout shared with all particle shaders (set=1, binding=0).
// Must match ParticleUniforms in every particle vertex shader.
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

    // ── particle GPU upload (copy pass must happen BEFORE render pass) ──────
    if (particleSystem)
        particleSystem->uploadToGpu(cmd);

    // ── ImGui vertex/index upload (also before render pass) ─────────────────
    ImDrawData* const k_drawData = ImGui::GetDrawData();
    if (k_drawData)
        ImGui_ImplSDLGPU3_PrepareDrawData(k_drawData, cmd);

    // ── render pass ─────────────────────────────────────────────────────────
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

    // ── 1. Scene geometry (cube + floor, model = identity) ──────────────────
    Matrices mats{};
    mats.model = glm::mat4(1.0f);
    mats.view = camera.getView();
    mats.projection = camera.getProjection();
    SDL_PushGPUVertexUniformData(cmd, 0, &mats, sizeof(mats));

    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_DrawGPUPrimitives(pass, 42, 1, 0, 0);

    // ── 2. Particles ─────────────────────────────────────────────────────────
    if (particleSystem) {
        // Re-push ParticleUniforms at slot 0 — overrides the Matrices push above.
        ParticleUniforms pu{};
        pu.view = camera.getView();
        pu.proj = camera.getProjection();
        pu.camPos = camera.getEye();
        pu.camRight = camera.getRight();
        pu.camUp = camera.getUp();
        SDL_PushGPUVertexUniformData(cmd, 0, &pu, sizeof(pu));

        particleSystem->render(pass, cmd);
    }

    // ── 3. ImGui overlay ─────────────────────────────────────────────────────
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
