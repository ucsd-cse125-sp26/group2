/// @file MenuTheme.hpp
/// @brief Shared visual theme + helpers for the front-end ImGui menus (home/host/lobby/pause).
///
/// All front-end menus share one cohesive dark/cyan look.  Call applyStyle() and
/// loadFonts() once after ImGui::CreateContext(); call drawBackground() each frame
/// from a menu screen (after ImGui::NewFrame()) to paint the responsive backdrop.
#pragma once

#include <imgui.h>

struct SDL_GPUDevice;

namespace menu_theme
{
struct ThemeSettings
{
    ImVec4 accent{0.20f, 0.80f, 0.94f, 1.00f};
    ImVec4 accentHover{0.38f, 0.90f, 1.00f, 1.00f};
    ImVec4 accentActive{0.12f, 0.55f, 0.66f, 1.00f};
    ImVec4 accentText{0.03f, 0.09f, 0.12f, 1.00f};
    ImVec4 text{0.88f, 0.93f, 0.97f, 1.00f};
    ImVec4 textDim{0.55f, 0.62f, 0.70f, 1.00f};
    ImVec4 windowBg{0.05f, 0.07f, 0.10f, 0.97f};
    ImVec4 childBg{0.08f, 0.11f, 0.15f, 0.85f};
    ImVec4 popupBg{0.06f, 0.08f, 0.12f, 0.98f};
    ImVec4 frameBg{0.12f, 0.16f, 0.21f, 1.00f};
    ImVec4 frameHover{0.16f, 0.22f, 0.28f, 1.00f};
    ImVec4 frameActive{0.20f, 0.28f, 0.35f, 1.00f};
    ImVec4 button{0.14f, 0.19f, 0.25f, 1.00f};
    ImVec4 buttonHover{0.20f, 0.42f, 0.50f, 1.00f};
    ImVec4 buttonActive{0.12f, 0.30f, 0.37f, 1.00f};
    ImVec4 header{0.16f, 0.30f, 0.38f, 1.00f};
    ImVec4 border{0.22f, 0.34f, 0.42f, 0.55f};
    ImVec4 danger{0.62f, 0.16f, 0.16f, 1.00f};
    ImVec4 dangerHover{0.78f, 0.20f, 0.20f, 1.00f};
    ImVec4 dangerActive{0.48f, 0.10f, 0.10f, 1.00f};
    ImVec4 titleBg{0.04f, 0.06f, 0.09f, 1.00f};
    ImVec4 titleBgActive{0.07f, 0.11f, 0.15f, 1.00f};
    ImVec4 titleBgCollapsed{0.04f, 0.06f, 0.09f, 0.80f};
    ImVec4 menuBarBg{0.08f, 0.11f, 0.15f, 1.00f};
    ImVec4 scrollbarBg{0.04f, 0.06f, 0.09f, 0.60f};
    ImVec4 tableHeaderBg{0.10f, 0.14f, 0.19f, 1.00f};
    ImVec4 tableBorderLight{0.18f, 0.24f, 0.30f, 0.40f};
    ImVec4 tableRowBg{0.00f, 0.00f, 0.00f, 0.00f};
    ImVec4 tableRowBgAlt{1.00f, 1.00f, 1.00f, 0.025f};
    float textSelectedAlpha = 0.35f;

    float windowRounding = 6.0f;
    float childRounding = 2.0f;
    float frameRounding = 2.0f;
    float popupRounding = 6.0f;
    float grabRounding = 1.0f;
    float scrollbarRounding = 2.0f;
    float windowBorderSize = 1.0f;
    float frameBorderSize = 0.0f;
    float popupBorderSize = 1.0f;
    ImVec2 windowPadding{22.0f, 20.0f};
    ImVec2 framePadding{12.0f, 7.0f};
    ImVec2 itemSpacing{10.0f, 9.0f};
    ImVec2 itemInnerSpacing{8.0f, 6.0f};
    ImVec2 cellPadding{8.0f, 6.0f};
    float scrollbarSize = 14.0f;
    float grabMinSize = 12.0f;
    ImVec2 windowTitleAlign{0.5f, 0.5f};

    float scaleBaseWidth = 1280.0f;
    float scaleBaseHeight = 720.0f;
    float minScale = 0.85f;
    float maxScale = 2.0f;
    float panelViewportMargin = 0.94f;
    float panelTitleScale = 1.9f;
    float panelTitleRuleOffsetY = 3.0f;
    float panelTitleRuleThickness = 2.0f;
    float panelTitleRuleRounding = 2.0f;
    float panelTitleBottomSpacing = 14.0f;
    float headingTopSpacing = 4.0f;
    float headingRuleOffsetY = 1.0f;
    float headingRuleThickness = 1.0f;
    float headingBottomSpacing = 6.0f;

    ImVec4 backgroundTop{18.0f / 255.0f, 26.0f / 255.0f, 38.0f / 255.0f, 1.0f};
    ImVec4 backgroundBottom{6.0f / 255.0f, 8.0f / 255.0f, 12.0f / 255.0f, 1.0f};
    float backgroundImageAlpha = 205.0f / 255.0f;
    ImVec4 backgroundImageOverlay{6.0f / 255.0f, 8.0f / 255.0f, 12.0f / 255.0f, 120.0f / 255.0f};
    ImVec4 backgroundGlow{22.0f / 255.0f, 92.0f / 255.0f, 112.0f / 255.0f, 70.0f / 255.0f};
    float backgroundGlowHeight = 0.45f;
};

/// @brief Mutable live menu theme settings used by applyStyle() and helpers.
ThemeSettings& settings();

/// @brief Apply the cohesive dark/cyan style (colors + rounding + spacing).
/// @note Call once after ImGui::CreateContext() (and after ImGui::StyleColorsDark()).
void applyStyle();

/// @brief Load the UI font (terminal font if present, otherwise SpaceGrotesk) and set it as the default.
///        Safe (no-op) if no font file is found.
void loadFonts();

/// @brief Pointer to the loaded terminal/CRT font, or nullptr if none was found by loadFonts().
ImFont* terminalFont();

/// @brief RAII helper that pushes terminalFont() for its lifetime.  No-op when the font is missing.
struct ScopedTerminalFont
{
    bool pushed = false;
    ScopedTerminalFont();
    ~ScopedTerminalFont();
    ScopedTerminalFont(const ScopedTerminalFont&) = delete;
    ScopedTerminalFont& operator=(const ScopedTerminalFont&) = delete;
};

/// @brief Responsive UI scale derived from the current display size (1.0 at 1280x720).
float scaleFor(const ImVec2& display);

/// @brief Begin a centered, responsive, chromeless panel.
/// @param idAndTitle Window id; also drawn as the panel title when showTitle is true.
/// @param baseWidth  Design width at 1280x720 (scaled up/down with the display).
/// @param baseHeight Design height at 1280x720.
/// @param showTitle  When true, draws a large accent title + underline rule.
/// @param extraFlags Additional ImGuiWindowFlags to OR in.
/// @return ImGui::Begin() result.  Always pair with endPanel(), even when this returns false.
bool beginPanel(
    const char* idAndTitle, float baseWidth, float baseHeight, bool showTitle, ImGuiWindowFlags extraFlags = 0);

/// @brief End a panel opened with beginPanel().
void endPanel();

/// @brief Accent section header with an underline rule (styled replacement for ImGui::SeparatorText).
void heading(const char* text);

/// @brief Bright accent call-to-action button.
bool accentButton(const char* label, const ImVec2& size = ImVec2(0, 0));

/// @brief Red destructive-action button.
bool dangerButton(const char* label, const ImVec2& size = ImVec2(0, 0));

/// @brief Draw the gradient + optional bg.webp background behind the current frame.
/// @param device GPU device used to upload the background image (may be null; gradient still draws).
/// @note Loads the image lazily on first call and silently falls back to the gradient when none is found.
void drawBackground(SDL_GPUDevice* device);

/// @brief Release GPU resources held for the background.  Call before the GPU device is destroyed.
void releaseBackground(SDL_GPUDevice* device);

/// @brief Live ImGui editor for every shared menu-theme parameter.
void buildTweaker(bool* open);
} // namespace menu_theme
