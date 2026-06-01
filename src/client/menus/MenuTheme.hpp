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
/// @brief Apply the cohesive dark/cyan style (colors + rounding + spacing).
/// @note Call once after ImGui::CreateContext() (and after ImGui::StyleColorsDark()).
void applyStyle();

/// @brief Load the SpaceGrotesk UI font and set it as the default.  Safe (no-op) if the file is missing.
void loadFonts();

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
} // namespace menu_theme
