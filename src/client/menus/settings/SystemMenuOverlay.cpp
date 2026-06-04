#include "SystemMenuOverlay.hpp"

#include "menus/MenuTheme.hpp"

#include <algorithm>
#include <imgui.h>

namespace
{
constexpr float k_rootWindowBaseWidth = 420.0f;
constexpr float k_rootWindowBaseHeight = 250.0f;
constexpr float k_settingsWindowBaseWidth = 900.0f;
constexpr float k_settingsWindowBaseHeight = 760.0f;
constexpr float k_viewportWindowMargin = 0.94f;
} // namespace

void SystemMenuOverlay::open()
{
    open_ = true;
}

void SystemMenuOverlay::close()
{
    open_ = false;
    settingsEditor_.close();
    confirm_.cancel();
}

bool SystemMenuOverlay::isOpen() const
{
    return open_;
}

void SystemMenuOverlay::handleEscape(const UserSettings& settings)
{
    if (!open_)
        return;

    if (confirm_.isOpen()) {
        confirm_.cancel();
        return;
    }

    if (settingsEditor_.isOpen()) {
        settingsEditor_.handleEscape(settings);
        return;
    }

    close();
}

bool SystemMenuOverlay::consumeEvent(const SDL_Event& event)
{
    if (!open_)
        return false;

    if (settingsEditor_.isOpen())
        return settingsEditor_.consumeEvent(event);

    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        return true;
    default:
        return false;
    }
}

SystemMenuOverlayResult SystemMenuOverlay::render(UserSettings& settings, std::string_view settingsPath)
{
    SystemMenuOverlayResult result{};
    if (!open_)
        return result;

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled({0.0f, 0.0f}, display, IM_COL32(0, 0, 0, 150));

    const bool settingsOpen = settingsEditor_.isOpen();
    const float scaled = std::min(menu_theme::scaleFor(display), 1.0f);
    const float baseWidth = settingsOpen ? k_settingsWindowBaseWidth : k_rootWindowBaseWidth;
    const float baseHeight = settingsOpen ? k_settingsWindowBaseHeight : k_rootWindowBaseHeight;
    ImVec2 windowSize{baseWidth * scaled, baseHeight * scaled};
    windowSize.x = std::min(windowSize.x, baseWidth);
    windowSize.x = std::min(windowSize.x, display.x * k_viewportWindowMargin);
    windowSize.y = std::min(windowSize.y, display.y * k_viewportWindowMargin);
    const float uiScale = std::clamp(windowSize.x / baseWidth, 0.5f, 1.0f);

    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("System Menu", nullptr, flags)) {
        ImGui::SetWindowFontScale(uiScale);
        if (settingsOpen) {
            const SettingsEditorResult settingsResult = settingsEditor_.render(settings, settingsPath, uiScale);
            result.settingsApplied = settingsResult.applied;
        } else {
            menu_theme::terminalStatusLine("SYSTEM MENU", "ESC CLOSES");
            menu_theme::terminalSection("COMMANDS");
            const float buttonWidth = ImGui::GetContentRegionAvail().x;
            if (menu_theme::terminalActionRow("BACK", "return to previous screen", {buttonWidth, 36.0f * uiScale})) {
                close();
            }
            if (menu_theme::terminalActionRow("SETTINGS", "open controls and options", {buttonWidth, 36.0f * uiScale}))
            {
                settingsEditor_.open(settings);
            }
            if (menu_theme::terminalActionRow(
                    "EXIT TO DESKTOP", "close application", {buttonWidth, 36.0f * uiScale}, true))
            {
                confirm_.open({.title = "Exit to Desktop?",
                               .message = "Exit the game and close the application?",
                               .confirmText = "Exit",
                               .cancelText = "Stay",
                               .confirmIsDanger = true});
            }
        }
    }
    ImGui::End();

    const ConfirmResult confirmResult = confirm_.drawAndPoll();
    if (confirmResult == ConfirmResult::Confirmed) {
        result.exitToDesktop = true;
    }

    return result;
}
