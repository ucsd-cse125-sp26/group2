#pragma once
#include <SDL3/SDL.h>

#include <glm/glm.hpp>
#include <vector>

// ---------------------------------------------------------------------------
// HUD — 2-D overlay rendered after the 3-D scene, on top of the swapchain.
//
// Renders with a separate pipeline (hud.vert / hud.frag):
//   • No depth test   • Alpha blend   • NDC [-1,1] coordinates
//
// Geometry is built CPU-side every frame into a dynamic vertex buffer
// and drawn in a single draw call.
// ---------------------------------------------------------------------------

struct HudVertex
{
    glm::vec2 pos;   // NDC
    glm::vec4 color; // RGBA
};

struct HudState
{
    int health       = 100; // 0-100
    int maxHealth    = 100;
    int armor        = 0;
    int ammo         = 30;
    int reserve      = 90;
    bool reloading   = false;
    float reloadFrac = 0.0f;     // 0-1

    float hitMarkerTimer = 0.0f; // shows hit marker for this many seconds
    bool killedSomeone   = false;

    // Grapple
    bool grappleActive = false;
};

class HUD
{
public:
    bool init(SDL_GPUDevice* gpu, SDL_Window* window);
    void destroy();

    // Upload and draw the HUD for this frame.
    // Must be called OUTSIDE a render pass (pushes uniforms), then inside.
    void
    draw(SDL_GPUCommandBuffer* cmdbuf, SDL_GPUTexture* swapchain, uint32_t winW, uint32_t winH, const HudState& state);

    void tickHitMarker(float dt);

private:
    SDL_GPUDevice* gpu              = nullptr;
    SDL_GPUGraphicsPipeline* pipe   = nullptr;
    SDL_GPUBuffer* vbuf             = nullptr;
    static constexpr int k_maxVerts = 2048;

    void buildGeometry(std::vector<HudVertex>& verts, uint32_t w, uint32_t h, const HudState& state) const;

    void addQuad(
        std::vector<HudVertex>& v, float x0, float y0, float x1, float y1, glm::vec4 col, uint32_t w, uint32_t h) const;
};
