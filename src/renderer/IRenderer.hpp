#pragma once

#include <glm/vec2.hpp>

struct SDL_Window;

// ---------------------------------------------------------------------------
// IRenderer — common interface shared by all rendering backends.
//
// Each backend (SDL3 GPU, OpenGL, …) implements this interface.
// Swap backends at configure time:
//   cmake --preset debug -DUSE_OPENGL=ON   → OpenGL 4.1 core
//   cmake --preset debug                   → SDL3 GPU pipeline (default)
// ---------------------------------------------------------------------------

class IRenderer
{
public:
    virtual ~IRenderer() = default;

    // Called once after the window is created.  Returns false on fatal error.
    virtual bool init(SDL_Window* window) = 0;

    // Called every frame.  Renders and presents one frame.
    // playerPos is in world space (+X = right, +Y = up).
    virtual void renderFrame(glm::vec2 playerPos) = 0;

    // Called before the window is destroyed.
    virtual void shutdown() = 0;
};
