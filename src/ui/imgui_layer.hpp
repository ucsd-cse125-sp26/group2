#pragma once
#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// ImGuiLayer — wraps Dear ImGui lifecycle for SDL3 GPU or OpenGL backend.
//
// In Release builds (NDEBUG defined) all methods are no-ops so ImGui draw
// calls compile out of shipping binaries.
//
// SDL3 GPU usage sequence per frame:
//   1. newFrame()               — backend + ImGui new-frame
//   2. (build ImGui UI)
//   3. endFrame()               — ImGui::Render() (finalises draw list)
//   4. prepareGPU(cmdBuf)       — upload vertex/index data BEFORE render pass
//   5. SDL_BeginGPURenderPass(...)
//   6. render(renderPass, cmdBuf) — rasterise into the open pass
//   7. SDL_EndGPURenderPass(...)
//
// OpenGL usage sequence per frame:
//   1. newFrame()
//   2. (build ImGui UI)
//   3. endFrame()               — ImGui::Render()
//   4. render(nullptr, nullptr) — ImGui_ImplOpenGL3_RenderDrawData
//   5. SDL_GL_SwapWindow(window) — done in main.cpp after render()
// ---------------------------------------------------------------------------
class ImGuiLayer
{
public:
    // Pass device=nullptr for the OpenGL backend.
    bool init(SDL_Window* window, SDL_GPUDevice* device);
    // Call once per frame before building ImGui UI.
    void newFrame();
    // Call after building the UI to finalise the ImGui frame (ImGui::Render).
    // Must be called every frame even when nothing will be drawn (e.g. swapchain
    // unavailable) to keep ImGui's internal frame counter consistent.
    void endFrame();
    // SDL3 GPU only: call after endFrame() but BEFORE SDL_BeginGPURenderPass.
    // Uploads vertex/index data into the command buffer.  No-op for OpenGL.
    void prepareGPU(SDL_GPUCommandBuffer* cmdBuf);
    // Call after building the UI; needs active render pass (SDL3 GPU)
    // or nullptr (OpenGL — does its own GL calls internally).
    void render(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmdBuf);
    void shutdown();
};
