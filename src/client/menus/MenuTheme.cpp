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
const menu_theme::ThemeSettings k_defaultSettings{};
menu_theme::ThemeSettings g_settings = k_defaultSettings;

struct BgResources
{
    bool tried = false;
    SDL_GPUTexture* tex = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    SDL_GPUTextureSamplerBinding binding{};
};
BgResources bgState;

ImFont* g_terminalFont = nullptr;

/// First terminal font file found on disk (relative to SDL base path or CWD).
ImFont* tryLoadTerminalFont(ImGuiIO& io, const std::string& base)
{
    // NOTE: Can add more terminal fonts here if desired, only one will be loaded.
    const std::string names[] = {"FSEX302.ttf"};
    for (const std::string& name : names) {
        std::string p1 = base;
        p1.append("fonts/").append(name);
        std::string p2 = base;
        p2.append("assets/fonts/").append(name);
        std::string p3 = "assets/fonts/";
        p3.append(name);
        const std::string candidates[] = {p1, p2, p3};
        for (const std::string& path : candidates) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
                continue;
            if (ImFont* f = io.Fonts->AddFontFromFileTTF(path.c_str(), 20.0f))
                return f;
        }
    }
    return nullptr;
}

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
ThemeSettings& settings()
{
    return g_settings;
}

void applyStyle()
{
    const ThemeSettings& t = g_settings;
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = t.windowRounding;
    s.ChildRounding = t.childRounding;
    s.FrameRounding = t.frameRounding;
    s.PopupRounding = t.popupRounding;
    s.GrabRounding = t.grabRounding;
    s.ScrollbarRounding = t.scrollbarRounding;
    s.WindowBorderSize = t.windowBorderSize;
    s.FrameBorderSize = t.frameBorderSize;
    s.PopupBorderSize = t.popupBorderSize;
    s.WindowPadding = t.windowPadding;
    s.FramePadding = t.framePadding;
    s.ItemSpacing = t.itemSpacing;
    s.ItemInnerSpacing = t.itemInnerSpacing;
    s.CellPadding = t.cellPadding;
    s.ScrollbarSize = t.scrollbarSize;
    s.GrabMinSize = t.grabMinSize;
    s.WindowTitleAlign = t.windowTitleAlign;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = t.text;
    c[ImGuiCol_TextDisabled] = t.textDim;
    c[ImGuiCol_WindowBg] = t.windowBg;
    c[ImGuiCol_ChildBg] = t.childBg;
    c[ImGuiCol_PopupBg] = t.popupBg;
    c[ImGuiCol_Border] = t.border;
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = t.frameBg;
    c[ImGuiCol_FrameBgHovered] = t.frameHover;
    c[ImGuiCol_FrameBgActive] = t.frameActive;
    c[ImGuiCol_TitleBg] = t.titleBg;
    c[ImGuiCol_TitleBgActive] = t.titleBgActive;
    c[ImGuiCol_TitleBgCollapsed] = t.titleBgCollapsed;
    c[ImGuiCol_MenuBarBg] = t.menuBarBg;
    c[ImGuiCol_ScrollbarBg] = t.scrollbarBg;
    c[ImGuiCol_ScrollbarGrab] = t.button;
    c[ImGuiCol_ScrollbarGrabHovered] = t.buttonHover;
    c[ImGuiCol_ScrollbarGrabActive] = t.accentActive;
    c[ImGuiCol_CheckMark] = t.accent;
    c[ImGuiCol_SliderGrab] = t.accent;
    c[ImGuiCol_SliderGrabActive] = t.accentHover;
    c[ImGuiCol_Button] = t.button;
    c[ImGuiCol_ButtonHovered] = t.buttonHover;
    c[ImGuiCol_ButtonActive] = t.buttonActive;
    c[ImGuiCol_Header] = t.header;
    c[ImGuiCol_HeaderHovered] = t.buttonHover;
    c[ImGuiCol_HeaderActive] = t.buttonActive;
    c[ImGuiCol_Separator] = t.border;
    c[ImGuiCol_SeparatorHovered] = t.accentActive;
    c[ImGuiCol_SeparatorActive] = t.accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = t.accentActive;
    c[ImGuiCol_ResizeGripActive] = t.accent;
    c[ImGuiCol_TableHeaderBg] = t.tableHeaderBg;
    c[ImGuiCol_TableBorderStrong] = t.border;
    c[ImGuiCol_TableBorderLight] = t.tableBorderLight;
    c[ImGuiCol_TableRowBg] = t.tableRowBg;
    c[ImGuiCol_TableRowBgAlt] = t.tableRowBgAlt;
    c[ImGuiCol_TextSelectedBg] = ImVec4(t.accent.x, t.accent.y, t.accent.z, t.textSelectedAlpha);
}

void loadFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    const char* base = SDL_GetBasePath();
    const std::string b = base ? base : "";

    ImFont* spaceGrotesk = nullptr;
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
            spaceGrotesk = f;
            break;
        }
    }

    g_terminalFont = tryLoadTerminalFont(io, b);

    if (g_terminalFont)
        io.FontDefault = g_terminalFont;
    else if (spaceGrotesk)
        io.FontDefault = spaceGrotesk;
    // Otherwise keep ImGui's built-in font (graceful, no error).
}

ImFont* terminalFont()
{
    return g_terminalFont;
}

ScopedTerminalFont::ScopedTerminalFont()
{
    if (g_terminalFont) {
        ImGui::PushFont(g_terminalFont);
        pushed = true;
    }
}

ScopedTerminalFont::~ScopedTerminalFont()
{
    if (pushed)
        ImGui::PopFont();
}

float scaleFor(const ImVec2& display)
{
    const ThemeSettings& t = g_settings;
    const float baseWidth = std::max(t.scaleBaseWidth, 1.0f);
    const float baseHeight = std::max(t.scaleBaseHeight, 1.0f);
    const float s = std::min(display.x / baseWidth, display.y / baseHeight);
    return std::clamp(s, t.minScale, std::max(t.minScale, t.maxScale));
}

bool beginPanel(const char* idAndTitle, float baseWidth, float baseHeight, bool showTitle, ImGuiWindowFlags extraFlags)
{
    const ImGuiIO& io = ImGui::GetIO();
    const float s = scaleFor(io.DisplaySize);

    ImVec2 size(baseWidth * s, baseHeight * s);
    const ThemeSettings& t = g_settings;
    const float viewportMargin = std::clamp(t.panelViewportMargin, 0.1f, 1.0f);
    size.x = std::min(size.x, io.DisplaySize.x * viewportMargin);
    size.y = std::min(size.y, io.DisplaySize.y * viewportMargin);

    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | extraFlags;
    const bool open = ImGui::Begin(idAndTitle, nullptr, flags);
    if (open) {
        ImGui::SetWindowFontScale(s);
        if (showTitle) {
            ImGui::SetWindowFontScale(s * t.panelTitleScale);
            const ImVec2 ts = ImGui::CalcTextSize(idAndTitle);
            const float winW = ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x * 2.0f;
            if (winW > ts.x)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (winW - ts.x) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, t.accent);
            ImGui::TextUnformatted(idAndTitle);
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(s);

            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float fullW = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(p.x, p.y + t.panelTitleRuleOffsetY),
                ImVec2(p.x + fullW, p.y + t.panelTitleRuleOffsetY + t.panelTitleRuleThickness),
                ImGui::GetColorU32(t.accent),
                t.panelTitleRuleRounding);
            ImGui::Dummy(ImVec2(0.0f, t.panelTitleBottomSpacing));
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
    const ThemeSettings& t = g_settings;
    ImGui::Dummy(ImVec2(0.0f, t.headingTopSpacing));
    ImGui::PushStyleColor(ImGuiCol_Text, t.accent);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float fullW = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + t.headingRuleOffsetY),
                                              ImVec2(p.x + fullW, p.y + t.headingRuleOffsetY + t.headingRuleThickness),
                                              ImGui::GetColorU32(t.border));
    ImGui::Dummy(ImVec2(0.0f, t.headingBottomSpacing));
}

bool accentButton(const char* label, const ImVec2& size)
{
    const ThemeSettings& t = g_settings;
    ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, t.accentText);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool dangerButton(const char* label, const ImVec2& size)
{
    const ThemeSettings& t = g_settings;
    ImGui::PushStyleColor(ImGuiCol_Button, t.danger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.dangerHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.dangerActive);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

void drawBackground(SDL_GPUDevice* device)
{
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 disp = io.DisplaySize;
    const ThemeSettings& t = g_settings;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Base diagonal gradient: deep navy at the top fading to near-black at the bottom.
    const ImU32 top = ImGui::ColorConvertFloat4ToU32(t.backgroundTop);
    const ImU32 bottom = ImGui::ColorConvertFloat4ToU32(t.backgroundBottom);
    dl->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), disp, top, top, bottom, bottom);

    // Lazy-load the background image exactly once.
    if (!bgState.tried && device) {
        bgState.tried = true;
        tryLoadBackground(device);
    }

    if (bgState.tex) {
        const auto id = (ImTextureID)(intptr_t)&bgState.binding;
        dl->AddImage(id,
                     ImVec2(0.0f, 0.0f),
                     disp,
                     ImVec2(0.0f, 0.0f),
                     ImVec2(1.0f, 1.0f),
                     ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, t.backgroundImageAlpha)));
        // Darken slightly so panel text stays legible over the photo.
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), disp, ImGui::GetColorU32(t.backgroundImageOverlay));
    }

    // Cyan glow band fading down from the top edge for a little depth.
    dl->AddRectFilledMultiColor(
        ImVec2(0.0f, 0.0f),
        ImVec2(disp.x, disp.y * std::clamp(t.backgroundGlowHeight, 0.0f, 1.0f)),
        ImGui::GetColorU32(t.backgroundGlow),
        ImGui::GetColorU32(t.backgroundGlow),
        ImGui::GetColorU32(ImVec4(t.backgroundGlow.x, t.backgroundGlow.y, t.backgroundGlow.z, 0.0f)),
        ImGui::GetColorU32(ImVec4(t.backgroundGlow.x, t.backgroundGlow.y, t.backgroundGlow.z, 0.0f)));
}

void buildTweaker(bool* open)
{
    if (open && !*open)
        return;

    ImGui::SetNextWindowSize({520.0f, 760.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Menu Theme Tweaker", open)) {
        ImGui::End();
        return;
    }

    ThemeSettings& t = g_settings;
    bool changed = false;
    constexpr ImGuiColorEditFlags k_colorFlags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;

    auto color = [&](const char* label, ImVec4& value) { changed |= ImGui::ColorEdit4(label, &value.x, k_colorFlags); };
    auto drag = [&](const char* label, float& value, float speed, float minValue, float maxValue, const char* fmt) {
        changed |= ImGui::DragFloat(label, &value, speed, minValue, maxValue, fmt);
    };
    auto drag2 = [&](const char* label, ImVec2& value, float speed, float minValue, float maxValue) {
        changed |= ImGui::DragFloat2(label, &value.x, speed, minValue, maxValue, "%.1f");
    };

    if (ImGui::Button("Reset to defaults")) {
        t = k_defaultSettings;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Changes apply live and are not saved to disk.");

    if (ImGui::CollapsingHeader("Core Palette", ImGuiTreeNodeFlags_DefaultOpen)) {
        color("Accent", t.accent);
        color("Accent Hover", t.accentHover);
        color("Accent Active", t.accentActive);
        color("Accent Text", t.accentText);
        color("Text", t.text);
        color("Text Dim", t.textDim);
        color("Window BG", t.windowBg);
        color("Child BG", t.childBg);
        color("Popup BG", t.popupBg);
        color("Border", t.border);
    }

    if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        color("Frame BG", t.frameBg);
        color("Frame Hover", t.frameHover);
        color("Frame Active", t.frameActive);
        color("Button", t.button);
        color("Button Hover", t.buttonHover);
        color("Button Active", t.buttonActive);
        color("Header", t.header);
        color("Danger", t.danger);
        color("Danger Hover", t.dangerHover);
        color("Danger Active", t.dangerActive);
        changed |= ImGui::SliderFloat("Text Selection Alpha", &t.textSelectedAlpha, 0.0f, 1.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Window Chrome")) {
        color("Title BG", t.titleBg);
        color("Title BG Active", t.titleBgActive);
        color("Title BG Collapsed", t.titleBgCollapsed);
        color("Menu Bar BG", t.menuBarBg);
        color("Scrollbar BG", t.scrollbarBg);
        color("Table Header BG", t.tableHeaderBg);
        color("Table Border Light", t.tableBorderLight);
        color("Table Row BG", t.tableRowBg);
        color("Table Row BG Alt", t.tableRowBgAlt);
    }

    if (ImGui::CollapsingHeader("Rounding And Borders", ImGuiTreeNodeFlags_DefaultOpen)) {
        drag("Window Rounding", t.windowRounding, 0.1f, 0.0f, 40.0f, "%.1f");
        drag("Child Rounding", t.childRounding, 0.1f, 0.0f, 40.0f, "%.1f");
        drag("Frame Rounding", t.frameRounding, 0.1f, 0.0f, 40.0f, "%.1f");
        drag("Popup Rounding", t.popupRounding, 0.1f, 0.0f, 40.0f, "%.1f");
        drag("Grab Rounding", t.grabRounding, 0.1f, 0.0f, 40.0f, "%.1f");
        drag("Scrollbar Rounding", t.scrollbarRounding, 0.1f, 0.0f, 40.0f, "%.1f");
        drag("Window Border Size", t.windowBorderSize, 0.05f, 0.0f, 8.0f, "%.2f");
        drag("Frame Border Size", t.frameBorderSize, 0.05f, 0.0f, 8.0f, "%.2f");
        drag("Popup Border Size", t.popupBorderSize, 0.05f, 0.0f, 8.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Spacing And Sizing", ImGuiTreeNodeFlags_DefaultOpen)) {
        drag2("Window Padding", t.windowPadding, 0.2f, 0.0f, 80.0f);
        drag2("Frame Padding", t.framePadding, 0.2f, 0.0f, 80.0f);
        drag2("Item Spacing", t.itemSpacing, 0.2f, 0.0f, 80.0f);
        drag2("Item Inner Spacing", t.itemInnerSpacing, 0.2f, 0.0f, 80.0f);
        drag2("Cell Text Padding", t.cellPadding, 0.2f, 0.0f, 80.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Padding between table cell walls and the text/content inside each cell.");
        drag("Scrollbar Size", t.scrollbarSize, 0.2f, 1.0f, 80.0f, "%.1f");
        drag("Grab Min Size", t.grabMinSize, 0.2f, 1.0f, 80.0f, "%.1f");
        changed |= ImGui::DragFloat2("Window Title Align", &t.windowTitleAlign.x, 0.01f, 0.0f, 1.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Panel Layout", ImGuiTreeNodeFlags_DefaultOpen)) {
        drag("Scale Base Width", t.scaleBaseWidth, 1.0f, 1.0f, 4096.0f, "%.0f");
        drag("Scale Base Height", t.scaleBaseHeight, 1.0f, 1.0f, 4096.0f, "%.0f");
        changed |= ImGui::SliderFloat("Min Scale", &t.minScale, 0.25f, 3.0f, "%.2f");
        changed |= ImGui::SliderFloat("Max Scale", &t.maxScale, 0.25f, 4.0f, "%.2f");
        t.maxScale = std::max(t.minScale, t.maxScale);
        changed |= ImGui::SliderFloat("Viewport Margin", &t.panelViewportMargin, 0.1f, 1.0f, "%.2f");
        drag("Title Font Multiplier", t.panelTitleScale, 0.01f, 0.1f, 5.0f, "%.2f");
        drag("Title Rule Offset Y", t.panelTitleRuleOffsetY, 0.1f, -20.0f, 60.0f, "%.1f");
        drag("Title Rule Thickness", t.panelTitleRuleThickness, 0.1f, 0.0f, 20.0f, "%.1f");
        drag("Title Rule Rounding", t.panelTitleRuleRounding, 0.1f, 0.0f, 20.0f, "%.1f");
        drag("Title Bottom Spacing", t.panelTitleBottomSpacing, 0.1f, 0.0f, 80.0f, "%.1f");
        drag("Heading Top Spacing", t.headingTopSpacing, 0.1f, 0.0f, 80.0f, "%.1f");
        drag("Heading Rule Offset Y", t.headingRuleOffsetY, 0.1f, -20.0f, 60.0f, "%.1f");
        drag("Heading Rule Thickness", t.headingRuleThickness, 0.1f, 0.0f, 20.0f, "%.1f");
        drag("Heading Bottom Spacing", t.headingBottomSpacing, 0.1f, 0.0f, 80.0f, "%.1f");
    }

    if (ImGui::CollapsingHeader("Background")) {
        color("Background Top", t.backgroundTop);
        color("Background Bottom", t.backgroundBottom);
        changed |= ImGui::SliderFloat("Image Alpha", &t.backgroundImageAlpha, 0.0f, 1.0f, "%.2f");
        color("Image Overlay", t.backgroundImageOverlay);
        color("Glow", t.backgroundGlow);
        changed |= ImGui::SliderFloat("Glow Height", &t.backgroundGlowHeight, 0.0f, 1.0f, "%.2f");
    }

    if (changed) {
        t.textSelectedAlpha = std::clamp(t.textSelectedAlpha, 0.0f, 1.0f);
        t.backgroundImageAlpha = std::clamp(t.backgroundImageAlpha, 0.0f, 1.0f);
        t.backgroundGlowHeight = std::clamp(t.backgroundGlowHeight, 0.0f, 1.0f);
        applyStyle();
    }

    ImGui::End();
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
