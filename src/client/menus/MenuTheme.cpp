/// @file MenuTheme.cpp
/// @brief Implementation of the shared front-end menu theme.

#include "MenuTheme.hpp"

#include "renderer-new/Boilerplate.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{
menu_theme::ThemeSettings makeGameplaySettings()
{
    menu_theme::ThemeSettings t;
    t.accent = ImVec4{0.20f, 0.80f, 0.94f, 1.00f};
    t.accentHover = ImVec4{0.38f, 0.90f, 1.00f, 1.00f};
    t.accentActive = ImVec4{0.12f, 0.55f, 0.66f, 1.00f};
    t.accentText = ImVec4{0.03f, 0.09f, 0.12f, 1.00f};
    t.text = ImVec4{0.88f, 0.93f, 0.97f, 1.00f};
    t.textDim = ImVec4{0.55f, 0.62f, 0.70f, 1.00f};
    t.windowBg = ImVec4{0.05f, 0.07f, 0.10f, 0.97f};
    t.childBg = ImVec4{0.08f, 0.11f, 0.15f, 0.85f};
    t.popupBg = ImVec4{0.06f, 0.08f, 0.12f, 0.98f};
    t.frameBg = ImVec4{0.12f, 0.16f, 0.21f, 1.00f};
    t.frameHover = ImVec4{0.16f, 0.22f, 0.28f, 1.00f};
    t.frameActive = ImVec4{0.20f, 0.28f, 0.35f, 1.00f};
    t.button = ImVec4{0.14f, 0.19f, 0.25f, 1.00f};
    t.buttonHover = ImVec4{0.20f, 0.42f, 0.50f, 1.00f};
    t.buttonActive = ImVec4{0.12f, 0.30f, 0.37f, 1.00f};
    t.header = ImVec4{0.16f, 0.30f, 0.38f, 1.00f};
    t.border = ImVec4{0.22f, 0.34f, 0.42f, 0.55f};
    t.danger = ImVec4{0.62f, 0.16f, 0.16f, 1.00f};
    t.dangerHover = ImVec4{0.78f, 0.20f, 0.20f, 1.00f};
    t.dangerActive = ImVec4{0.48f, 0.10f, 0.10f, 1.00f};
    t.titleBg = ImVec4{0.04f, 0.06f, 0.09f, 1.00f};
    t.titleBgActive = ImVec4{0.07f, 0.11f, 0.15f, 1.00f};
    t.titleBgCollapsed = ImVec4{0.04f, 0.06f, 0.09f, 0.80f};
    t.menuBarBg = ImVec4{0.08f, 0.11f, 0.15f, 1.00f};
    t.scrollbarBg = ImVec4{0.04f, 0.06f, 0.09f, 0.60f};
    t.tableHeaderBg = ImVec4{0.10f, 0.14f, 0.19f, 1.00f};
    t.tableBorderLight = ImVec4{0.18f, 0.24f, 0.30f, 0.40f};
    t.tableRowBgAlt = ImVec4{1.00f, 1.00f, 1.00f, 0.025f};
    t.textSelectedAlpha = 0.35f;

    t.windowRounding = 6.0f;
    t.childRounding = 2.0f;
    t.frameRounding = 2.0f;
    t.popupRounding = 6.0f;
    t.grabRounding = 1.0f;
    t.scrollbarRounding = 2.0f;
    t.frameBorderSize = 0.0f;
    t.windowPadding = ImVec2{22.0f, 20.0f};
    t.framePadding = ImVec2{12.0f, 7.0f};
    t.itemSpacing = ImVec2{10.0f, 9.0f};
    t.scrollbarSize = 14.0f;
    t.panelTitleRuleRounding = 2.0f;

    t.backgroundTop = ImVec4{18.0f / 255.0f, 26.0f / 255.0f, 38.0f / 255.0f, 1.0f};
    t.backgroundBottom = ImVec4{6.0f / 255.0f, 8.0f / 255.0f, 12.0f / 255.0f, 1.0f};
    t.backgroundImageAlpha = 205.0f / 255.0f;
    t.backgroundImageOverlay = ImVec4{6.0f / 255.0f, 8.0f / 255.0f, 12.0f / 255.0f, 120.0f / 255.0f};
    t.backgroundGlow = ImVec4{22.0f / 255.0f, 92.0f / 255.0f, 112.0f / 255.0f, 70.0f / 255.0f};
    t.backgroundGlowHeight = 0.45f;
    return t;
}

const menu_theme::ThemeSettings k_terminalSettings{};
const menu_theme::ThemeSettings k_gameplaySettings = makeGameplaySettings();
menu_theme::ThemeSettings g_settings = k_terminalSettings;

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

const ThemeSettings& terminalSettings()
{
    return k_terminalSettings;
}

const ThemeSettings& gameplaySettings()
{
    return k_gameplaySettings;
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
    c[ImGuiCol_Tab] = ImVec4(0.18f, 0.18f, 0.17f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.34f, 0.34f, 0.31f, 1.0f);
    c[ImGuiCol_TabActive] = ImVec4(0.52f, 0.52f, 0.48f, 1.0f);
    c[ImGuiCol_TabUnfocused] = ImVec4(0.14f, 0.14f, 0.13f, 1.0f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.46f, 0.46f, 0.42f, 1.0f);
    c[ImGuiCol_NavHighlight] = t.accent;
    c[ImGuiCol_TextSelectedBg] = ImVec4(t.accent.x, t.accent.y, t.accent.z, t.textSelectedAlpha);
}

ScopedTheme::ScopedTheme(const ThemeSettings& theme) : previous(g_settings)
{
    g_settings = theme;
    applyStyle();
}

ScopedTheme::~ScopedTheme()
{
    g_settings = previous;
    applyStyle();
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

bool beginScrollBody(const char* id, float footerHeight)
{
    return ImGui::BeginChild(id, ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NavFlattened);
}

void endScrollBody()
{
    ImGui::EndChild();
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

void terminalSection(const char* text)
{
    const ThemeSettings& t = g_settings;
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, t.textDim);
    ImGui::Text(":: %s", text);
    ImGui::PopStyleColor();

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float fullW = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(p.x, p.y + 1.0f), ImVec2(p.x + fullW, p.y + 2.0f), ImGui::GetColorU32(t.border));
    ImGui::Dummy(ImVec2(0.0f, 7.0f));
}

bool terminalActionRow(const char* command, const char* description, const ImVec2& size, bool danger)
{
    const ThemeSettings& t = g_settings;
    char label[512];
    if (description && description[0] != '\0') {
        std::snprintf(label, sizeof(label), "> %-18s  %s", command, description);
    } else {
        std::snprintf(label, sizeof(label), "> %s", command);
    }

    ImGui::PushStyleColor(ImGuiCol_Button, danger ? t.danger : t.header);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, danger ? t.dangerHover : t.buttonHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, danger ? t.dangerActive : t.buttonActive);
    ImGui::PushStyleColor(ImGuiCol_Text, danger ? ImVec4{1.0f, 0.82f, 0.78f, 1.0f} : t.text);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImVec2 rowSize = size;
    if (rowSize.x <= 0.0f)
        rowSize.x = ImGui::GetContentRegionAvail().x;
    const bool pressed = ImGui::Button(label, rowSize);
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    if (ImGui::IsItemFocused()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect(min, max, ImGui::GetColorU32(t.accent), 0.0f, 0, 1.0f);
    }

    return pressed;
}

void terminalStatusLine(const char* left, const char* right)
{
    const ThemeSettings& t = g_settings;
    ImGui::Spacing();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float fullW = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(p.x, p.y), ImVec2(p.x + fullW, p.y + 1.0f), ImGui::GetColorU32(t.border));
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    ImGui::PushStyleColor(ImGuiCol_Text, t.textDim);
    ImGui::TextUnformatted(left ? left : "");
    if (right && right[0] != '\0') {
        const ImVec2 rightSize = ImGui::CalcTextSize(right);
        const float x = ImGui::GetCursorPosX() + std::max(0.0f, fullW - rightSize.x);
        ImGui::SameLine(x);
        ImGui::TextUnformatted(right);
    }
    ImGui::PopStyleColor();
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

    // Base gradient: almost black, with just enough lift for white panel outlines.
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
        // Darken heavily so white panel chrome dominates over any loaded image.
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), disp, ImGui::GetColorU32(t.backgroundImageOverlay));
    }

    const float glowHeight = std::clamp(t.backgroundGlowHeight, 0.0f, 1.0f);
    if (glowHeight > 0.0f && t.backgroundGlow.w > 0.0f) {
        dl->AddRectFilledMultiColor(
            ImVec2(0.0f, 0.0f),
            ImVec2(disp.x, disp.y * glowHeight),
            ImGui::GetColorU32(t.backgroundGlow),
            ImGui::GetColorU32(t.backgroundGlow),
            ImGui::GetColorU32(ImVec4(t.backgroundGlow.x, t.backgroundGlow.y, t.backgroundGlow.z, 0.0f)),
            ImGui::GetColorU32(ImVec4(t.backgroundGlow.x, t.backgroundGlow.y, t.backgroundGlow.z, 0.0f)));
    }

    const ImU32 scanline = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.025f));
    for (float y = 0.0f; y < disp.y; y += 8.0f) {
        dl->AddLine(ImVec2(0.0f, y), ImVec2(disp.x, y), scanline);
    }
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
        t = k_terminalSettings;
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
