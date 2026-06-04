/// @file MenuTheme.hpp
/// @brief Shared visual theme + helpers for the front-end ImGui menus (home/host/lobby/pause).
///
/// All front-end menus share one cohesive white terminal look.  Call applyStyle() and
/// loadFonts() once after ImGui::CreateContext(); call drawBackground() each frame
/// from a menu screen (after ImGui::NewFrame()) to paint the responsive backdrop.
#pragma once

#include <imgui.h>

struct SDL_GPUDevice;

namespace menu_theme
{
struct ThemeSettings
{
    ImVec4 accent{0.96f, 0.96f, 0.92f, 1.00f};
    ImVec4 accentHover{1.00f, 1.00f, 1.00f, 1.00f};
    ImVec4 accentActive{0.72f, 0.72f, 0.68f, 1.00f};
    ImVec4 accentText{0.02f, 0.02f, 0.02f, 1.00f};
    ImVec4 text{0.96f, 0.96f, 0.92f, 1.00f};
    ImVec4 textDim{0.58f, 0.60f, 0.58f, 1.00f};
    ImVec4 windowBg{0.01f, 0.01f, 0.01f, 0.92f};
    ImVec4 childBg{0.02f, 0.02f, 0.02f, 0.72f};
    ImVec4 popupBg{0.01f, 0.01f, 0.01f, 0.98f};
    ImVec4 frameBg{0.02f, 0.02f, 0.02f, 0.96f};
    ImVec4 frameHover{0.10f, 0.10f, 0.10f, 1.00f};
    ImVec4 frameActive{0.18f, 0.18f, 0.17f, 1.00f};
    ImVec4 button{0.02f, 0.02f, 0.02f, 0.96f};
    ImVec4 buttonHover{0.13f, 0.13f, 0.12f, 1.00f};
    ImVec4 buttonActive{0.22f, 0.22f, 0.20f, 1.00f};
    ImVec4 header{0.08f, 0.08f, 0.08f, 0.98f};
    ImVec4 border{0.88f, 0.88f, 0.82f, 0.72f};
    ImVec4 danger{0.36f, 0.04f, 0.04f, 1.00f};
    ImVec4 dangerHover{0.58f, 0.08f, 0.08f, 1.00f};
    ImVec4 dangerActive{0.86f, 0.18f, 0.16f, 1.00f};
    ImVec4 titleBg{0.01f, 0.01f, 0.01f, 1.00f};
    ImVec4 titleBgActive{0.03f, 0.03f, 0.03f, 1.00f};
    ImVec4 titleBgCollapsed{0.01f, 0.01f, 0.01f, 0.86f};
    ImVec4 menuBarBg{0.02f, 0.02f, 0.02f, 1.00f};
    ImVec4 scrollbarBg{0.01f, 0.01f, 0.01f, 0.70f};
    ImVec4 tableHeaderBg{0.04f, 0.04f, 0.04f, 1.00f};
    ImVec4 tableBorderLight{0.72f, 0.72f, 0.68f, 0.42f};
    ImVec4 tableRowBg{0.00f, 0.00f, 0.00f, 0.00f};
    ImVec4 tableRowBgAlt{1.00f, 1.00f, 1.00f, 0.045f};
    float textSelectedAlpha = 0.28f;

    float windowRounding = 0.0f;
    float childRounding = 0.0f;
    float frameRounding = 0.0f;
    float popupRounding = 0.0f;
    float grabRounding = 0.0f;
    float scrollbarRounding = 0.0f;
    float windowBorderSize = 1.0f;
    float frameBorderSize = 1.0f;
    float popupBorderSize = 1.0f;
    ImVec2 windowPadding{24.0f, 20.0f};
    ImVec2 framePadding{12.0f, 8.0f};
    ImVec2 itemSpacing{10.0f, 10.0f};
    ImVec2 itemInnerSpacing{8.0f, 6.0f};
    ImVec2 cellPadding{8.0f, 6.0f};
    float scrollbarSize = 12.0f;
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
    float panelTitleRuleRounding = 0.0f;
    float panelTitleBottomSpacing = 14.0f;
    float headingTopSpacing = 4.0f;
    float headingRuleOffsetY = 1.0f;
    float headingRuleThickness = 1.0f;
    float headingBottomSpacing = 6.0f;

    ImVec4 backgroundTop{0.03f, 0.03f, 0.03f, 1.0f};
    ImVec4 backgroundBottom{0.0f, 0.0f, 0.0f, 1.0f};
    float backgroundImageAlpha = 0.35f;
    ImVec4 backgroundImageOverlay{0.0f, 0.0f, 0.0f, 0.78f};
    ImVec4 backgroundGlow{0.96f, 0.96f, 0.92f, 0.0f};
    float backgroundGlowHeight = 0.0f;
};

/// @brief Mutable live menu theme settings used by applyStyle() and helpers.
ThemeSettings& settings();

/// @brief Front-end white terminal menu defaults.
const ThemeSettings& terminalSettings();

/// @brief In-game bluish menu defaults that match the current HUD palette.
const ThemeSettings& gameplaySettings();

/// @brief Apply the cohesive white terminal style (colors + rounding + spacing).
/// @note Call once after ImGui::CreateContext() (and after ImGui::StyleColorsDark()).
void applyStyle();

/// @brief Temporarily replace the live menu theme and restore it when leaving scope.
struct ScopedTheme
{
    ThemeSettings previous;

    explicit ScopedTheme(const ThemeSettings& theme);
    ~ScopedTheme();
    ScopedTheme(const ScopedTheme&) = delete;
    ScopedTheme& operator=(const ScopedTheme&) = delete;
};

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

/// @brief Terminal-style section prompt/header.
void terminalSection(const char* text);

/// @brief Selectable command row for keyboard/gamepad/mouse-driven terminal menus.
bool terminalActionRow(const char* command,
                       const char* description = nullptr,
                       const ImVec2& size = ImVec2(0, 0),
                       bool danger = false);

/// @brief Draw a dim one-line terminal status strip.
void terminalStatusLine(const char* left, const char* right = nullptr);

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
