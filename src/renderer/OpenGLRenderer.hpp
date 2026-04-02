#pragma once

#include "IRenderer.hpp"

#include <SDL3/SDL.h>

#include <glm/vec2.hpp>

// ---------------------------------------------------------------------------
// OpenGLRenderer — RGB triangle via OpenGL 4.1 core profile.
//
// Enable at configure time:  cmake --preset debug -DUSE_OPENGL=ON
//
// GLSL shaders are compiled at C++ compile time (inline string literals),
// so no external shader files or compilation tools are needed for this
// backend.  Function loading uses glad (fetched automatically by CMake).
// ---------------------------------------------------------------------------

class OpenGLRenderer : public IRenderer
{
public:
    bool init(SDL_Window* window) override;
    void renderFrame(glm::vec2 playerPos) override;
    void shutdown() override;

private:
    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;
    unsigned int vao = 0;
    unsigned int shaderProgram = 0;
    int offsetLoc = -1; // location of the uOffset uniform
};
