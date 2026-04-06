#include "imgui_layer.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#ifdef USE_OPENGL
#include <imgui_impl_opengl3.h>
#else
#include <imgui_impl_sdlgpu3.h>
#endif

bool ImGuiLayer::init(SDL_Window* window, SDL_GPUDevice* device)
{
#ifdef NDEBUG
    (void)window;
    (void)device;
    return true;
#else
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

#ifdef USE_OPENGL
    if (!ImGui_ImplSDL3_InitForOpenGL(window, SDL_GL_GetCurrentContext()))
        return false;
#else
    if (!ImGui_ImplSDL3_InitForSDLGPU(window))
        return false;
#endif

#ifdef USE_OPENGL
    (void)device;
    if (!ImGui_ImplOpenGL3_Init("#version 410 core"))
        return false;
#else
    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = device;
    info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&info))
        return false;
#endif
    return true;
#endif
}

void ImGuiLayer::newFrame()
{
#ifndef NDEBUG
#ifdef USE_OPENGL
    ImGui_ImplOpenGL3_NewFrame();
#else
    ImGui_ImplSDLGPU3_NewFrame();
#endif
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
#endif
}

void ImGuiLayer::endFrame()
{
#ifndef NDEBUG
    ImGui::Render();
#endif
}

void ImGuiLayer::prepareGPU(SDL_GPUCommandBuffer* cmdBuf)
{
#ifndef NDEBUG
#ifndef USE_OPENGL
    Imgui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmdBuf);
#else
    (void)cmdBuf;
#endif
#else
    (void)cmdBuf;
#endif
}

void ImGuiLayer::render(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmdBuf)
{
#ifndef NDEBUG
#ifdef USE_OPENGL
    (void)renderPass;
    (void)cmdBuf;
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#else
    ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmdBuf, renderPass);
#endif
#else
    (void)renderPass;
    (void)cmdBuf;
#endif
}

void ImGuiLayer::shutdown()
{
#ifndef NDEBUG
#ifdef USE_OPENGL
    ImGui_ImplOpenGL3_Shutdown();
#else
    ImGui_ImplSDLGPU3_Shutdown();
#endif
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
#endif
}
