#include "PauseMenu.hpp"

#include "menus/MenuTheme.hpp"

#include <algorithm>
#include <imgui.h>

namespace
{
constexpr float k_settingsWindowBaseWidth = 900.0f;
constexpr float k_settingsWindowBaseHeight = 860.0f;
constexpr float k_viewportWindowMargin = 0.94f;
} // namespace

void PauseMenu::open()
{
    menuOpen = true;
    menu_theme::playUiSound(UiSoundAction::ModalOpen);
}

void PauseMenu::close()
{
    if (menuOpen)
        menu_theme::playUiSound(UiSoundAction::ModalClose);
    menuOpen = false;
    settingsEditor_.close();
    pendingConfirm_ = PendingConfirm::None;
    confirm_.cancel();
}

bool PauseMenu::isOpen() const
{
    return menuOpen;
}

bool PauseMenu::isSettingsOpen() const
{
    return menuOpen && settingsEditor_.isOpen();
}

bool PauseMenu::handleEscape(const UserSettings& settings)
{
    if (confirm_.isOpen()) {
        confirm_.cancel();
        pendingConfirm_ = PendingConfirm::None;
        return false;
    }
    if (settingsEditor_.isOpen()) {
        settingsEditor_.handleEscape(settings);
        return false;
    }
    return true;
}

void PauseMenu::openSettings(const UserSettings& settings)
{
    settingsEditor_.open(settings);
}

bool PauseMenu::consumeEvent(const SDL_Event& event)
{
    if (!menuOpen)
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

PauseMenuResult PauseMenu::render(UserSettings& settings, std::string_view settingsPath)
{
    PauseMenuResult result{};
    if (!menuOpen)
        return result;

    menu_theme::ScopedTheme gameplayTheme(menu_theme::gameplaySettings());

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    background->AddRectFilled({0.0f, 0.0f}, display, IM_COL32(0, 0, 0, 150));

    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    float settingsUiScale = 1.0f;
    if (settingsEditor_.isOpen()) {
        flags |= ImGuiWindowFlags_NoResize;
        const float scaled = std::min(menu_theme::scaleFor(display), 1.0f);
        ImVec2 settingsSize{k_settingsWindowBaseWidth * scaled, k_settingsWindowBaseHeight * scaled};
        settingsSize.x = std::min(settingsSize.x, k_settingsWindowBaseWidth);
        settingsSize.x = std::min(settingsSize.x, display.x * k_viewportWindowMargin);
        settingsSize.y = std::min(settingsSize.y, display.y * k_viewportWindowMargin);
        settingsUiScale = std::clamp(settingsSize.x / k_settingsWindowBaseWidth, 0.5f, 1.0f);
        ImGui::SetNextWindowSize(settingsSize, ImGuiCond_Always);
    } else {
        flags |= ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }

    if (ImGui::Begin("Paused", nullptr, flags)) {
        ImGui::SetWindowFontScale(settingsEditor_.isOpen() ? settingsUiScale : 1.0f);

        const float buttonWidth = settingsEditor_.isOpen() ? ImGui::GetContentRegionAvail().x : 360.0f;
        const float btnH = std::max(36.0f, ImGui::GetFontSize() + 16.0f);
        if (!settingsEditor_.isOpen()) {
            if (menu_theme::accentButton("Resume", {buttonWidth, btnH})) {
                result.resumeGame = true;
            }

            ImGui::Spacing();
            if (ImGui::Button("Settings", {buttonWidth, btnH})) {
                openSettings(settings);
            }

            ImGui::Spacing();
            const float halfWidth = (buttonWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Leave Match", {halfWidth, btnH})) {
                pendingConfirm_ = PendingConfirm::LeaveMatch;
                confirm_.open({.title = "Leave Match?",
                               .message = "Leave the current match and return to the main menu?",
                               .confirmText = "Leave Match",
                               .cancelText = "Stay",
                               .confirmIsDanger = true});
            }
            ImGui::SameLine();
            if (menu_theme::dangerButton("Exit to Desktop", {halfWidth, btnH})) {
                pendingConfirm_ = PendingConfirm::ExitDesktop;
                confirm_.open({.title = "Exit to Desktop?",
                               .message = "Exit the game and close the application?",
                               .confirmText = "Exit",
                               .cancelText = "Stay",
                               .confirmIsDanger = true});
            }
        } else {
            const SettingsEditorResult settingsResult = settingsEditor_.render(settings, settingsPath, settingsUiScale);
            if (settingsResult.applied) {
                result.settingsApplied = true;
            }
        }
    }
    ImGui::End();

    const ConfirmResult confirmResult = confirm_.drawAndPoll();
    if (confirmResult == ConfirmResult::Confirmed) {
        switch (pendingConfirm_) {
        case PendingConfirm::LeaveMatch:
            result.returnToMainMenu = true;
            break;
        case PendingConfirm::ExitDesktop:
            result.exitToDesktop = true;
            break;
        case PendingConfirm::None:
            break;
        }
        pendingConfirm_ = PendingConfirm::None;
    } else if (confirmResult == ConfirmResult::Cancelled) {
        pendingConfirm_ = PendingConfirm::None;
    }

    return result;
}
