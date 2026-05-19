#include "PauseMenu.hpp"

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <imgui.h>

void PauseMenu::open()
{
    menuOpen = true;
}

void PauseMenu::close()
{
    menuOpen = false;
    settingsOpen = false;
    listeningAction.reset();
    dirty = false;
    statusMessage.clear();
    pendingConfirm_ = PendingConfirm::None;
    confirm_.cancel();
}

bool PauseMenu::isOpen() const
{
    return menuOpen;
}

bool PauseMenu::isSettingsOpen() const
{
    return menuOpen && settingsOpen;
}

bool PauseMenu::handleEscape()
{
    if (confirm_.isOpen()) {
        confirm_.cancel();
        pendingConfirm_ = PendingConfirm::None;
        return false;
    }
    if (listeningAction) {
        listeningAction.reset();
        return false;
    }
    if (settingsOpen) {
        if (dirty) {
            requestDiscardSettingsConfirm();
        } else {
            closeSettingsPage();
        }
        return false;
    }
    return true;
}

void PauseMenu::openSettings(const UserSettings& settings)
{
    settingsOpen = true;
    draftBindings = settings.inputBindings;
    draftMouseSensitivity = settings.mouseSensitivity;
    dirty = false;
    listeningAction.reset();
    statusMessage.clear();
}

void PauseMenu::closeSettingsPage()
{
    settingsOpen = false;
    listeningAction.reset();
    statusMessage.clear();
    dirty = false;
}

void PauseMenu::requestDiscardSettingsConfirm()
{
    listeningAction.reset();
    pendingConfirm_ = PendingConfirm::DiscardSettings;
    confirm_.open({.title = "Discard Settings?",
                   .message = "You have unsaved settings changes. Discard them?",
                   .confirmText = "Discard",
                   .cancelText = "Keep Editing",
                   .confirmIsDanger = true});
}

bool PauseMenu::consumeEvent(const SDL_Event& event)
{
    if (!menuOpen)
        return false;

    if (listeningAction) {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            if (event.key.key == SDLK_ESCAPE) {
                listeningAction.reset();
            } else if (event.key.key == SDLK_BACKSPACE || event.key.key == SDLK_DELETE) {
                draftBindings.rebind(*listeningAction, Binding::unbound());
                listeningAction.reset();
                dirty = true;
            } else {
                draftBindings.rebind(*listeningAction, Binding::bindKeyboard(event.key.scancode));
                listeningAction.reset();
                dirty = true;
            }
            return true;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            MouseButton button = MouseButton::None;
            switch (event.button.button) {
            case SDL_BUTTON_LEFT:
                button = MouseButton::Left;
                break;
            case SDL_BUTTON_MIDDLE:
                button = MouseButton::Middle;
                break;
            case SDL_BUTTON_RIGHT:
                button = MouseButton::Right;
                break;
            case SDL_BUTTON_X1:
                button = MouseButton::X1;
                break;
            case SDL_BUTTON_X2:
                button = MouseButton::X2;
                break;
            default:
                break;
            }
            if (button != MouseButton::None) {
                draftBindings.rebind(*listeningAction, Binding::bindMouse(button));
                dirty = true;
            }
            listeningAction.reset();
            return true;
        }
        return event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
               event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL ||
               event.type == SDL_EVENT_TEXT_INPUT;
    }

    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_TEXT_INPUT:
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

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    background->AddRectFilled({0.0f, 0.0f}, display, IM_COL32(0, 0, 0, 150));

    const ImVec2 windowSize = settingsOpen ? ImVec2{560.0f, 620.0f} : ImVec2{420.0f, 250.0f};
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Paused", nullptr, flags)) {
        const float buttonWidth = ImGui::GetContentRegionAvail().x;
        if (!settingsOpen) {
            ImGui::TextUnformatted("Game Paused");
            ImGui::Separator();

            if (ImGui::Button("Resume", {buttonWidth, 36.0f})) {
                result.resumeGame = true;
            }

            ImGui::Spacing();
            if (ImGui::Button("Settings", {buttonWidth, 36.0f})) {
                openSettings(settings);
            }

            ImGui::Spacing();
            const float halfWidth = (buttonWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Leave Match", {halfWidth, 36.0f})) {
                pendingConfirm_ = PendingConfirm::LeaveMatch;
                confirm_.open({.title = "Leave Match?",
                               .message = "Leave the current match and return to the main menu?",
                               .confirmText = "Leave Match",
                               .cancelText = "Stay",
                               .confirmIsDanger = true});
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.58f, 0.12f, 0.12f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.72f, 0.16f, 0.16f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.45f, 0.08f, 0.08f, 1.0f});
            if (ImGui::Button("Exit to Desktop", {halfWidth, 36.0f})) {
                pendingConfirm_ = PendingConfirm::ExitDesktop;
                confirm_.open({.title = "Exit to Desktop?",
                               .message = "Exit the game and close the application?",
                               .confirmText = "Exit",
                               .cancelText = "Stay",
                               .confirmIsDanger = true});
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::TextUnformatted("Settings");
            ImGui::Separator();

            const float previousSensitivity = draftMouseSensitivity;
            ImGui::SliderFloat("Mouse Sensitivity", &draftMouseSensitivity, 0.0001f, 0.005f, "%.4f");
            draftMouseSensitivity = std::clamp(draftMouseSensitivity, 0.0001f, 0.005f);
            if (draftMouseSensitivity != previousSensitivity)
                dirty = true;

            ImGui::Spacing();
            if (ImGui::BeginTable("bindings", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                ImGui::TableHeadersRow();

                for (Action action : InputBindings::actions()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(InputBindings::actionLabel(action).data());
                    ImGui::TableSetColumnIndex(1);
                    const bool listening = listeningAction && *listeningAction == action;
                    const std::string label = listening ? std::string("Press a key or button")
                                                        : InputBindings::bindingLabel(draftBindings.get(action));
                    ImGui::PushID(static_cast<int>(action));
                    if (ImGui::Button(label.c_str(), {-1.0f, 0.0f})) {
                        listeningAction = action;
                        statusMessage.clear();
                    }
                    ImGui::PopID();
                }

                ImGui::EndTable();
            }

            if (!statusMessage.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", statusMessage.c_str());
            }

            ImGui::Spacing();
            if (ImGui::Button("Reset to Defaults", {buttonWidth, 30.0f})) {
                draftBindings = InputBindings::defaults();
                draftMouseSensitivity = 0.0007f;
                listeningAction.reset();
                dirty = true;
                statusMessage.clear();
            }

            const float halfWidth = (buttonWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Apply", {halfWidth, 34.0f})) {
                settings.inputBindings = draftBindings;
                settings.mouseSensitivity = draftMouseSensitivity;
                const bool saved = user_settings::save(std::string(settingsPath), settings);
                statusMessage = saved ? "Settings saved." : "Settings could not be saved.";
                dirty = false;
                listeningAction.reset();
                result.settingsApplied = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {halfWidth, 34.0f})) {
                if (dirty) {
                    requestDiscardSettingsConfirm();
                } else {
                    closeSettingsPage();
                }
            }

            if (dirty) {
                ImGui::TextUnformatted("Unsaved changes");
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
        case PendingConfirm::DiscardSettings:
            draftBindings = settings.inputBindings;
            draftMouseSensitivity = settings.mouseSensitivity;
            closeSettingsPage();
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
