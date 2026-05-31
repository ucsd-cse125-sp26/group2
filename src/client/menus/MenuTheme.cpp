/// @file MenuTheme.cpp
/// @brief Implementation of the shared front-end menu theme.

#include "MenuTheme.hpp"

#include "renderer-new/Boilerplate.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>

namespace
{
// ─── Palette (dark navy base, cyan accent — matches the in-game HUD) ──────────
const ImVec4 k_accent(0.20f, 0.80f, 0.94f, 1.00f);
const ImVec4 k_accentHover(0.38f, 0.90f, 1.00f, 1.00f);
const ImVec4 k_accentActive(0.12f, 0.55f, 0.66f, 1.00f);
const ImVec4 k_accentText(0.03f, 0.09f, 0.12f, 1.00f); ///< Dark text drawn on bright accent fills.
const ImVec4 k_text(0.88f, 0.93f, 0.97f, 1.00f);
const ImVec4 k_textDim(0.55f, 0.62f, 0.70f, 1.00f);
const ImVec4 k_windowBg(0.05f, 0.07f, 0.10f, 0.97f);
const ImVec4 k_childBg(0.08f, 0.11f, 0.15f, 0.85f);
const ImVec4 k_frameBg(0.12f, 0.16f, 0.21f, 1.00f);
const ImVec4 k_frameHover(0.16f, 0.22f, 0.28f, 1.00f);
const ImVec4 k_frameActive(0.20f, 0.28f, 0.35f, 1.00f);
const ImVec4 k_button(0.14f, 0.19f, 0.25f, 1.00f);
const ImVec4 k_buttonHover(0.20f, 0.42f, 0.50f, 1.00f);
const ImVec4 k_buttonActive(0.12f, 0.30f, 0.37f, 1.00f);
const ImVec4 k_header(0.16f, 0.30f, 0.38f, 1.00f);
const ImVec4 k_border(0.22f, 0.34f, 0.42f, 0.55f);
const ImVec4 k_danger(0.62f, 0.16f, 0.16f, 1.00f);
const ImVec4 k_dangerHover(0.78f, 0.20f, 0.20f, 1.00f);
const ImVec4 k_dangerActive(0.48f, 0.10f, 0.10f, 1.00f);

struct BgResources
{
    bool tried = false;
    SDL_GPUTexture* tex = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    SDL_GPUTextureSamplerBinding binding{};
};
BgResources bgState;

/// Try the conventional background-image names; first decodable one wins.
bool tryLoadBackground(SDL_GPUDevice* device)
{
    // loadTexture() already roots relative paths at SDL_GetBasePath().
    const char* candidates[] = {"bg.webp", "assets/bg.webp", "bg.png", "assets/bg.png", "bg.jpg", "assets/bg.jpg"};
    for (const char* path : candidates) {
        if (SDL_GPUTexture* t = Boilerplate::loadTexture(device, path)) {
            bgState.tex = t;
            break;
        }
    }
    if (!bgState.tex)
        return false;

    bgState.sampler = Boilerplate::createLinearClampSampler(device);
    if (!bgState.sampler) {
        SDL_ReleaseGPUTexture(device, bgState.tex);
        bgState.tex = nullptr;
        return false;
    }
    bgState.binding = Boilerplate::makeTextureSamplerBinding(bgState.tex, bgState.sampler);
    return true;
}
} // namespace

namespace menu_theme
{
void applyStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 12.0f;
    s.ChildRounding = 8.0f;
    s.FrameRounding = 6.0f;
    s.PopupRounding = 8.0f;
    s.GrabRounding = 6.0f;
    s.ScrollbarRounding = 8.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;
    s.WindowPadding = ImVec2(22.0f, 20.0f);
    s.FramePadding = ImVec2(12.0f, 7.0f);
    s.ItemSpacing = ImVec2(10.0f, 9.0f);
    s.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    s.CellPadding = ImVec2(8.0f, 6.0f);
    s.ScrollbarSize = 14.0f;
    s.GrabMinSize = 12.0f;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = k_text;
    c[ImGuiCol_TextDisabled] = k_textDim;
    c[ImGuiCol_WindowBg] = k_windowBg;
    c[ImGuiCol_ChildBg] = k_childBg;
    c[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.08f, 0.12f, 0.98f);
    c[ImGuiCol_Border] = k_border;
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = k_frameBg;
    c[ImGuiCol_FrameBgHovered] = k_frameHover;
    c[ImGuiCol_FrameBgActive] = k_frameActive;
    c[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.06f, 0.09f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.11f, 0.15f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.04f, 0.06f, 0.09f, 0.8f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.11f, 0.15f, 1.0f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.04f, 0.06f, 0.09f, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = k_button;
    c[ImGuiCol_ScrollbarGrabHovered] = k_buttonHover;
    c[ImGuiCol_ScrollbarGrabActive] = k_accentActive;
    c[ImGuiCol_CheckMark] = k_accent;
    c[ImGuiCol_SliderGrab] = k_accent;
    c[ImGuiCol_SliderGrabActive] = k_accentHover;
    c[ImGuiCol_Button] = k_button;
    c[ImGuiCol_ButtonHovered] = k_buttonHover;
    c[ImGuiCol_ButtonActive] = k_buttonActive;
    c[ImGuiCol_Header] = k_header;
    c[ImGuiCol_HeaderHovered] = k_buttonHover;
    c[ImGuiCol_HeaderActive] = k_buttonActive;
    c[ImGuiCol_Separator] = k_border;
    c[ImGuiCol_SeparatorHovered] = k_accentActive;
    c[ImGuiCol_SeparatorActive] = k_accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = k_accentActive;
    c[ImGuiCol_ResizeGripActive] = k_accent;
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.10f, 0.14f, 0.19f, 1.0f);
    c[ImGuiCol_TableBorderStrong] = k_border;
    c[ImGuiCol_TableBorderLight] = ImVec4(0.18f, 0.24f, 0.30f, 0.4f);
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.025f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(k_accent.x, k_accent.y, k_accent.z, 0.35f);
}

void loadFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    const char* base = SDL_GetBasePath();
    const std::string b = base ? base : "";
    const std::string candidates[] = {
        b + "fonts/SpaceGrotesk.ttf",
        b + "assets/fonts/SpaceGrotesk.ttf",
        "assets/fonts/SpaceGrotesk.ttf",
    };
    for (const std::string& path : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            continue; // Pre-check avoids ImGui's missing-file assertion in debug builds.
        if (ImFont* f = io.Fonts->AddFontFromFileTTF(path.c_str(), 20.0f)) {
            io.FontDefault = f;
            return;
        }
    }
    // No font file present: keep ImGui's built-in font (graceful, no error).
}

float scaleFor(const ImVec2& display)
{
    const float s = std::min(display.x / 1280.0f, display.y / 720.0f);
    return std::clamp(s, 0.85f, 2.0f);
}

bool beginPanel(const char* idAndTitle, float baseWidth, float baseHeight, bool showTitle, ImGuiWindowFlags extraFlags)
{
    const ImGuiIO& io = ImGui::GetIO();
    const float s = scaleFor(io.DisplaySize);

    ImVec2 size(baseWidth * s, baseHeight * s);
    size.x = std::min(size.x, io.DisplaySize.x * 0.94f);
    size.y = std::min(size.y, io.DisplaySize.y * 0.94f);

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | extraFlags;
    const bool open = ImGui::Begin(idAndTitle, nullptr, flags);
    if (open) {
        ImGui::SetWindowFontScale(s);
        if (showTitle) {
            ImGui::SetWindowFontScale(s * 1.9f);
            const ImVec2 ts = ImGui::CalcTextSize(idAndTitle);
            const float winW = ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x * 2.0f;
            if (winW > ts.x)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (winW - ts.x) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, k_accent);
            ImGui::TextUnformatted(idAndTitle);
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(s);

            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float fullW = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(p.x, p.y + 3.0f), ImVec2(p.x + fullW, p.y + 5.0f), ImGui::GetColorU32(k_accent), 2.0f);
            ImGui::Dummy(ImVec2(0.0f, 14.0f));
        }
    }
    return open;
}

void endPanel()
{
    ImGui::End();
}

void heading(const char* text)
{
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, k_accent);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float fullW = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(p.x, p.y + 1.0f), ImVec2(p.x + fullW, p.y + 2.0f), ImGui::GetColorU32(k_border));
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
}

bool accentButton(const char* label, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, k_accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_accentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, k_accentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, k_accentText);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool dangerButton(const char* label, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, k_danger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_dangerHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, k_dangerActive);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

void drawBackground(SDL_GPUDevice* device)
{
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 disp = io.DisplaySize;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Base diagonal gradient: deep navy at the top fading to near-black at the bottom.
    const ImU32 top = IM_COL32(18, 26, 38, 255);
    const ImU32 bottom = IM_COL32(6, 8, 12, 255);
    dl->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), disp, top, top, bottom, bottom);

    // Lazy-load the background image exactly once.
    if (!bgState.tried && device) {
        bgState.tried = true;
        tryLoadBackground(device);
    }

    if (bgState.tex) {
        const auto id = (ImTextureID)(intptr_t)&bgState.binding;
        dl->AddImage(id, ImVec2(0.0f, 0.0f), disp, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 205));
        // Darken slightly so panel text stays legible over the photo.
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), disp, IM_COL32(6, 8, 12, 120));
    }

    // Cyan glow band fading down from the top edge for a little depth.
    dl->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f),
                                ImVec2(disp.x, disp.y * 0.45f),
                                IM_COL32(22, 92, 112, 70),
                                IM_COL32(22, 92, 112, 70),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0));
}

void releaseBackground(SDL_GPUDevice* device)
{
    if (device) {
        if (bgState.tex)
            SDL_ReleaseGPUTexture(device, bgState.tex);
        if (bgState.sampler)
            SDL_ReleaseGPUSampler(device, bgState.sampler);
    }
    bgState.tex = nullptr;
    bgState.sampler = nullptr;
    bgState.tried = false;
    bgState.binding = SDL_GPUTextureSamplerBinding{};
}
} // namespace menu_theme
