#include "HUD.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// init — build the HUD graphics pipeline
// ---------------------------------------------------------------------------

bool HUD::init(SDL_GPUDevice* device, SDL_Window* window)
{
    gpu = device;

    // ---- Load shaders ----
    auto loadShader = [&](const char* filename, SDL_GPUShaderStage stage) -> SDL_GPUShader* {
        const char* base = SDL_GetBasePath();
        std::string path = (base ? base : "./");
        path += "shaders/";
        path += filename;
        size_t sz  = 0;
        void* code = SDL_LoadFile(path.c_str(), &sz);
        if (!code) {
            SDL_Log("HUD: cannot load shader '%s': %s", path.c_str(), SDL_GetError());
            return nullptr;
        }
        SDL_GPUShaderCreateInfo info{};
        info.code                = static_cast<const Uint8*>(code);
        info.code_size           = sz;
        info.entrypoint          = "main";
        info.format              = SDL_GPU_SHADERFORMAT_SPIRV;
        info.stage               = stage;
        info.num_uniform_buffers = 0;
        SDL_GPUShader* sh        = SDL_CreateGPUShader(gpu, &info);
        SDL_free(code);
        return sh;
    };

#ifdef TITANDOOM_BUNDLE_SHADERS
    // Embedded-shader path mirrors the file-based path's logic
    (void)loadShader; // suppress unused-lambda warning
    // We still need the helper to compile; the GameServer uses file-based loading.
    // For simplicity the HUD falls back to file-based even in bundled builds:
    // both SPVs are tiny and the HUD is client-only.
#endif

    SDL_GPUShader* vert = loadShader("hud.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader* frag = loadShader("hud.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!vert || !frag) {
        SDL_Log("HUD: shader load failed");
        return false;
    }

    // ---- Vertex layout: vec2 pos + vec4 color ----
    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot       = 0;
    vbDesc.pitch      = sizeof(HudVertex);
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[2] = {};
    attrs[0].location               = 0;
    attrs[0].buffer_slot            = 0;
    attrs[0].format                 = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[0].offset                 = 0;
    attrs[1].location               = 1;
    attrs[1].buffer_slot            = 0;
    attrs[1].format                 = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attrs[1].offset                 = sizeof(glm::vec2);

    SDL_GPUGraphicsPipelineCreateInfo ci{};
    ci.vertex_shader   = vert;
    ci.fragment_shader = frag;

    ci.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
    ci.vertex_input_state.num_vertex_buffers         = 1;
    ci.vertex_input_state.vertex_attributes          = attrs;
    ci.vertex_input_state.num_vertex_attributes      = 2;

    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    // No depth test for HUD
    ci.depth_stencil_state.enable_depth_test  = false;
    ci.depth_stencil_state.enable_depth_write = false;

    // Alpha blend
    SDL_GPUColorTargetDescription colorTgt{};
    colorTgt.format = SDL_GetGPUSwapchainTextureFormat(gpu, window);
    // Use UNDEFINED format fallback — SDL will inherit swapchain format
    colorTgt.blend_state.enable_blend          = true;
    colorTgt.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colorTgt.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorTgt.blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
    colorTgt.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorTgt.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    colorTgt.blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;

    ci.target_info.color_target_descriptions = &colorTgt;
    ci.target_info.num_color_targets         = 1;
    ci.target_info.has_depth_stencil_target  = false;

    pipe = SDL_CreateGPUGraphicsPipeline(gpu, &ci);
    SDL_ReleaseGPUShader(gpu, vert);
    SDL_ReleaseGPUShader(gpu, frag);
    if (!pipe) {
        SDL_Log("HUD: pipeline create failed: %s", SDL_GetError());
        return false;
    }

    // ---- Dynamic vertex buffer ----
    SDL_GPUBufferCreateInfo bufCI{};
    bufCI.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufCI.size  = k_maxVerts * sizeof(HudVertex);
    vbuf        = SDL_CreateGPUBuffer(gpu, &bufCI);
    if (!vbuf) {
        SDL_Log("HUD: vertex buffer create failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

void HUD::destroy()
{
    if (vbuf) {
        SDL_ReleaseGPUBuffer(gpu, vbuf);
        vbuf = nullptr;
    }
    if (pipe) {
        SDL_ReleaseGPUGraphicsPipeline(gpu, pipe);
        pipe = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Geometry helpers — all coordinates in screen pixels, converted to NDC
// ---------------------------------------------------------------------------

void HUD::addQuad(
    std::vector<HudVertex>& v, float x0, float y0, float x1, float y1, glm::vec4 col, uint32_t w, uint32_t h) const
{
    auto toNDC = [&](float x, float y) -> glm::vec2 {
        return {x / static_cast<float>(w) * 2.0f - 1.0f, 1.0f - y / static_cast<float>(h) * 2.0f};
    };
    glm::vec2 tl = toNDC(x0, y0), tr = toNDC(x1, y0);
    glm::vec2 bl = toNDC(x0, y1), br = toNDC(x1, y1);

    v.push_back({tl, col});
    v.push_back({tr, col});
    v.push_back({br, col});
    v.push_back({tl, col});
    v.push_back({br, col});
    v.push_back({bl, col});
}

// ---------------------------------------------------------------------------
// Build all HUD quads for one frame
// ---------------------------------------------------------------------------

void HUD::buildGeometry(std::vector<HudVertex>& verts, uint32_t w, uint32_t h, const HudState& state) const
{
    float fw = static_cast<float>(w);
    float fh = static_cast<float>(h);
    float cx = fw * 0.5f;
    float cy = fh * 0.5f;

    // ---- Crosshair ----
    constexpr float k_xhairLen = 12.0f;
    constexpr float k_xhairThk = 2.0f;
    constexpr float k_xhairGap = 5.0f;
    glm::vec4 xhairCol         = {1.0f, 1.0f, 1.0f, 0.85f};

    // Left
    addQuad(verts,
            cx - k_xhairGap - k_xhairLen,
            cy - k_xhairThk * 0.5f,
            cx - k_xhairGap,
            cy + k_xhairThk * 0.5f,
            xhairCol,
            w,
            h);
    // Right
    addQuad(verts,
            cx + k_xhairGap,
            cy - k_xhairThk * 0.5f,
            cx + k_xhairGap + k_xhairLen,
            cy + k_xhairThk * 0.5f,
            xhairCol,
            w,
            h);
    // Top
    addQuad(verts,
            cx - k_xhairThk * 0.5f,
            cy - k_xhairGap - k_xhairLen,
            cx + k_xhairThk * 0.5f,
            cy - k_xhairGap,
            xhairCol,
            w,
            h);
    // Bottom
    addQuad(verts,
            cx - k_xhairThk * 0.5f,
            cy + k_xhairGap,
            cx + k_xhairThk * 0.5f,
            cy + k_xhairGap + k_xhairLen,
            xhairCol,
            w,
            h);

    // ---- Hit marker ----
    if (state.hitMarkerTimer > 0.0f) {
        constexpr float k_hmLen = 10.0f;
        constexpr float k_hmThk = 2.0f;
        float alpha             = std::min(state.hitMarkerTimer * 5.0f, 1.0f);
        glm::vec4 hmCol = state.killedSomeone ? glm::vec4{1.0f, 0.2f, 0.2f, alpha} : glm::vec4{1.0f, 0.8f, 0.1f, alpha};

        addQuad(verts, cx - k_hmLen, cy - k_hmThk * 0.5f, cx - 4.0f, cy + k_hmThk * 0.5f, hmCol, w, h);
        addQuad(verts, cx + 4.0f, cy - k_hmThk * 0.5f, cx + k_hmLen, cy + k_hmThk * 0.5f, hmCol, w, h);
        addQuad(verts, cx - k_hmThk * 0.5f, cy - k_hmLen, cx + k_hmThk * 0.5f, cy - 4.0f, hmCol, w, h);
        addQuad(verts, cx - k_hmThk * 0.5f, cy + 4.0f, cx + k_hmThk * 0.5f, cy + k_hmLen, hmCol, w, h);
    }

    // ---- Health bar (bottom-left) ----
    constexpr float k_barW = 200.0f;
    constexpr float k_barH = 14.0f;
    constexpr float k_barX = 20.0f;
    float barY             = fh - 40.0f;

    // Background
    addQuad(verts,
            k_barX - 1.0f,
            barY - 1.0f,
            k_barX + k_barW + 1.0f,
            barY + k_barH + 1.0f,
            {0.0f, 0.0f, 0.0f, 0.55f},
            w,
            h);

    // Health fill — green → yellow → red
    float hpFrac    = std::clamp(static_cast<float>(state.health) / static_cast<float>(state.maxHealth), 0.0f, 1.0f);
    glm::vec4 hpCol = hpFrac > 0.5f ? glm::vec4{1.0f - (hpFrac - 0.5f) * 2.0f, 0.85f, 0.1f, 0.9f}
                                    : glm::vec4{0.9f, hpFrac * 2.0f * 0.85f, 0.1f, 0.9f};
    addQuad(verts, k_barX, barY, k_barX + k_barW * hpFrac, barY + k_barH, hpCol, w, h);

    // Armor bar (slightly above health bar)
    if (state.armor > 0) {
        float armorFrac = std::clamp(static_cast<float>(state.armor) / 100.0f, 0.0f, 1.0f);
        addQuad(
            verts, k_barX - 1.0f, barY - 9.0f, k_barX + k_barW + 1.0f, barY - 4.0f, {0.0f, 0.0f, 0.0f, 0.45f}, w, h);
        addQuad(verts, k_barX, barY - 9.0f, k_barX + k_barW * armorFrac, barY - 4.0f, {0.4f, 0.7f, 1.0f, 0.85f}, w, h);
    }

    // ---- Ammo (bottom-right) ----
    float ammoX = fw - 160.0f;
    float ammoY = fh - 40.0f;

    // Ammo dots
    constexpr float k_dotR  = 5.0f;
    constexpr float k_dotSp = 12.0f;
    int dotCols             = std::min(state.ammo, 30);
    for (int i = 0; i < dotCols; ++i) {
        float dx     = ammoX + static_cast<float>(i % 10) * k_dotSp;
        float dy     = ammoY - static_cast<float>(i / 10) * k_dotSp;
        glm::vec4 dc = state.reloading ? glm::vec4{0.7f, 0.7f, 0.2f, 0.7f} : glm::vec4{0.95f, 0.95f, 0.95f, 0.85f};
        addQuad(verts, dx, dy, dx + k_dotR, dy + k_dotR, dc, w, h);
    }

    // Reload progress bar
    if (state.reloading) {
        addQuad(verts,
                ammoX,
                ammoY + 8.0f,
                ammoX + 120.0f * state.reloadFrac,
                ammoY + 14.0f,
                {0.9f, 0.8f, 0.1f, 0.8f},
                w,
                h);
    }

    // Reserve ammo text substitute: small dim dots
    for (int i = 0; i < std::min(state.reserve, 30); ++i) {
        float dx = ammoX + static_cast<float>(i % 10) * 7.0f;
        float dy = ammoY + 20.0f + static_cast<float>(i / 10) * 7.0f;
        addQuad(verts, dx, dy, dx + 3.0f, dy + 3.0f, {0.5f, 0.5f, 0.5f, 0.5f}, w, h);
    }

    // ---- Grapple indicator ----
    if (state.grappleActive) {
        addQuad(verts, cx - 3.0f, cy - 3.0f, cx + 3.0f, cy + 3.0f, {0.3f, 0.8f, 1.0f, 0.9f}, w, h);
    }

    // ---- Muzzle flash — brief full-screen vignette around the crosshair ----
    if (state.muzzleFlashTimer > 0.0f) {
        float frac = state.muzzleFlashTimer / 0.07f; // 1 → 0
        float r    = 70.0f + 80.0f * frac;           // grows outward from crosshair
        float a    = 0.55f * frac;
        addQuad(verts, cx - r, cy - r, cx + r, cy + r, {1.0f, 0.7f, 0.2f, a}, w, h);
    }
}

// ---------------------------------------------------------------------------
// draw — upload geometry and render
// ---------------------------------------------------------------------------

void HUD::draw(
    SDL_GPUCommandBuffer* cmdbuf, SDL_GPUTexture* swapchain, uint32_t winW, uint32_t winH, const HudState& state)
{
    if (!pipe || !vbuf)
        return;

    std::vector<HudVertex> verts;
    verts.reserve(256);
    buildGeometry(verts, winW, winH, state);

    if (verts.empty())
        return;

    int count = std::min(static_cast<int>(verts.size()), k_maxVerts);

    // Upload vertices via a transfer buffer
    SDL_GPUTransferBufferCreateInfo tbCI{};
    tbCI.usage                  = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbCI.size                   = static_cast<Uint32>(count * sizeof(HudVertex));
    SDL_GPUTransferBuffer* tbuf = SDL_CreateGPUTransferBuffer(gpu, &tbCI);
    if (!tbuf)
        return;

    void* mapped = SDL_MapGPUTransferBuffer(gpu, tbuf, false);
    std::memcpy(mapped, verts.data(), count * sizeof(HudVertex));
    SDL_UnmapGPUTransferBuffer(gpu, tbuf);

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmdbuf);
    SDL_GPUTransferBufferLocation src{tbuf, 0};
    SDL_GPUBufferRegion dst{vbuf, 0, static_cast<Uint32>(count * sizeof(HudVertex))};
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu, tbuf);

    // HUD render pass (no depth target, LOAD swapchain contents)
    SDL_GPUColorTargetInfo color{};
    color.texture  = swapchain;
    color.load_op  = SDL_GPU_LOADOP_LOAD; // preserve 3-D scene
    color.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmdbuf, &color, 1, nullptr);
    if (!pass)
        return;

    SDL_BindGPUGraphicsPipeline(pass, pipe);
    SDL_GPUBufferBinding binding{vbuf, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    SDL_DrawGPUPrimitives(pass, static_cast<Uint32>(count), 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

void HUD::tickHitMarker(float dt)
{
    // Called by the main loop
    (void)dt;
}
