#include "PauseMenu.hpp"

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <imgui.h>

namespace
{
constexpr float k_minMouseSensitivity = 0.0001f;
constexpr float k_maxMouseSensitivity = 0.005f;
constexpr float k_minFovDegrees = 50.0f;
constexpr float k_maxFovDegrees = 120.0f;
constexpr float k_minGamepadSensitivity = 1.0f;
constexpr float k_maxGamepadSensitivity = 20.0f;
constexpr float k_minGamepadLookDeadzone = 0.0f;
constexpr float k_maxGamepadLookDeadzone = 0.4f;
constexpr float k_minGamepadMoveDeadzone = 0.0f;
constexpr float k_maxGamepadMoveDeadzone = 0.5f;
constexpr float k_minAimAssistStrength = 0.0f;
constexpr float k_maxAimAssistStrength = 1.5f;

MouseButton mouseButtonFromSdl(uint8_t button)
{
    switch (button) {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    case SDL_BUTTON_X1:
        return MouseButton::X1;
    case SDL_BUTTON_X2:
        return MouseButton::X2;
    default:
        return MouseButton::None;
    }
}

std::optional<Binding> keyboardMouseBindingFromEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        return Binding::bindKeyboard(event.key.scancode);
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const MouseButton button = mouseButtonFromSdl(event.button.button);
        if (button != MouseButton::None)
            return Binding::bindMouse(button);
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        if (event.wheel.y > 0.0f)
            return Binding::bindMouseWheel(MouseWheelDirection::Up);
        if (event.wheel.y < 0.0f)
            return Binding::bindMouseWheel(MouseWheelDirection::Down);
    }
    return std::nullopt;
}

std::optional<Binding> controllerBindingFromEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        return Binding::bindGamepadButton(static_cast<SDL_GamepadButton>(event.gbutton.button));
    if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        if (event.gaxis.value < 16384)
            return std::nullopt;
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
            return Binding::bindGamepadAxis(GamepadAxisBinding::LeftTrigger);
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
            return Binding::bindGamepadAxis(GamepadAxisBinding::RightTrigger);
    }
    return std::nullopt;
}
} // namespace

void PauseMenu::open()
{
    menuOpen = true;
}

void PauseMenu::close()
{
    menuOpen = false;
    settingsOpen = false;
    listeningBinding.reset();
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
    if (listeningBinding) {
        listeningBinding.reset();
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
    draftHorizontalFovDegrees = settings.horizontalFovDegrees;
    draftShowControllerBindings = settings.showControllerBindings;
    draftGamepadYawSensitivity = settings.gamepadYawSensitivity;
    draftGamepadPitchSensitivity = settings.gamepadPitchSensitivity;
    draftGamepadLookDeadzone = settings.gamepadLookDeadzone;
    draftGamepadMoveDeadzone = settings.gamepadMoveDeadzone;
    draftAimAssistEnabled = settings.aimAssistEnabled;
    draftAimAssistStrength = settings.aimAssistStrength;
    draftGamepadSwapSticks = settings.gamepadSwapSticks;
    dirty = false;
    listeningBinding.reset();
    statusMessage.clear();
}

void PauseMenu::closeSettingsPage()
{
    settingsOpen = false;
    listeningBinding.reset();
    statusMessage.clear();
    dirty = false;
}

void PauseMenu::requestDiscardSettingsConfirm()
{
    listeningBinding.reset();
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

    if (listeningBinding) {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            if (event.key.key == SDLK_ESCAPE) {
                listeningBinding.reset();
            } else if (event.key.key == SDLK_BACKSPACE || event.key.key == SDLK_DELETE) {
                draftBindings.rebind(
                    listeningBinding->action, Binding::unbound(), listeningBinding->device, listeningBinding->slot);
                listeningBinding.reset();
                dirty = true;
            } else {
                if (listeningBinding->device == BindingDevice::KeyboardMouse) {
                    draftBindings.rebind(listeningBinding->action,
                                         Binding::bindKeyboard(event.key.scancode),
                                         listeningBinding->device,
                                         listeningBinding->slot);
                    listeningBinding.reset();
                    dirty = true;
                }
            }
            return true;
        }

        const std::optional<Binding> candidate = listeningBinding->device == BindingDevice::Controller
                                                     ? controllerBindingFromEvent(event)
                                                     : keyboardMouseBindingFromEvent(event);
        if (candidate) {
            draftBindings.rebind(
                listeningBinding->action, *candidate, listeningBinding->device, listeningBinding->slot);
            listeningBinding.reset();
            dirty = true;
            return true;
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_WHEEL ||
            event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
        {
            return true;
        }
        return event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
               event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL ||
               event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP ||
               event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
    }

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

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    background->AddRectFilled({0.0f, 0.0f}, display, IM_COL32(0, 0, 0, 150));

    const ImVec2 windowSize = settingsOpen ? ImVec2{740.0f, 760.0f} : ImVec2{420.0f, 250.0f};
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
            ImGui::SliderFloat(
                "Mouse Sensitivity", &draftMouseSensitivity, k_minMouseSensitivity, k_maxMouseSensitivity, "%.4f");
            draftMouseSensitivity = std::clamp(draftMouseSensitivity, k_minMouseSensitivity, k_maxMouseSensitivity);
            if (draftMouseSensitivity != previousSensitivity)
                dirty = true;

            const float previousFov = draftHorizontalFovDegrees;
            ImGui::SliderFloat("Horizontal FOV", &draftHorizontalFovDegrees, k_minFovDegrees, k_maxFovDegrees, "%.0f");
            draftHorizontalFovDegrees = std::clamp(draftHorizontalFovDegrees, k_minFovDegrees, k_maxFovDegrees);
            if (draftHorizontalFovDegrees != previousFov)
                dirty = true;

            ImGui::Spacing();
            ImGui::SeparatorText("Controller");

            const float previousGamepadYawSensitivity = draftGamepadYawSensitivity;
            ImGui::SliderFloat("Horizontal Look Sensitivity",
                               &draftGamepadYawSensitivity,
                               k_minGamepadSensitivity,
                               k_maxGamepadSensitivity,
                               "%.1f rad/s");
            draftGamepadYawSensitivity =
                std::clamp(draftGamepadYawSensitivity, k_minGamepadSensitivity, k_maxGamepadSensitivity);
            if (draftGamepadYawSensitivity != previousGamepadYawSensitivity)
                dirty = true;

            const float previousGamepadPitchSensitivity = draftGamepadPitchSensitivity;
            ImGui::SliderFloat("Vertical Look Sensitivity",
                               &draftGamepadPitchSensitivity,
                               k_minGamepadSensitivity,
                               k_maxGamepadSensitivity,
                               "%.1f rad/s");
            draftGamepadPitchSensitivity =
                std::clamp(draftGamepadPitchSensitivity, k_minGamepadSensitivity, k_maxGamepadSensitivity);
            if (draftGamepadPitchSensitivity != previousGamepadPitchSensitivity)
                dirty = true;

            const float previousGamepadLookDeadzone = draftGamepadLookDeadzone;
            ImGui::SliderFloat("Right Stick Deadzone",
                               &draftGamepadLookDeadzone,
                               k_minGamepadLookDeadzone,
                               k_maxGamepadLookDeadzone,
                               "%.2f");
            draftGamepadLookDeadzone =
                std::clamp(draftGamepadLookDeadzone, k_minGamepadLookDeadzone, k_maxGamepadLookDeadzone);
            if (draftGamepadLookDeadzone != previousGamepadLookDeadzone)
                dirty = true;

            const float previousGamepadMoveDeadzone = draftGamepadMoveDeadzone;
            ImGui::SliderFloat("Left Stick Deadzone",
                               &draftGamepadMoveDeadzone,
                               k_minGamepadMoveDeadzone,
                               k_maxGamepadMoveDeadzone,
                               "%.2f");
            draftGamepadMoveDeadzone =
                std::clamp(draftGamepadMoveDeadzone, k_minGamepadMoveDeadzone, k_maxGamepadMoveDeadzone);
            if (draftGamepadMoveDeadzone != previousGamepadMoveDeadzone)
                dirty = true;

            const bool previousGamepadSwapSticks = draftGamepadSwapSticks;
            ImGui::Checkbox("Swap Sticks (southpaw)", &draftGamepadSwapSticks);
            if (draftGamepadSwapSticks != previousGamepadSwapSticks)
                dirty = true;

            const bool previousAimAssistEnabled = draftAimAssistEnabled;
            ImGui::Checkbox("Aim Assist", &draftAimAssistEnabled);
            if (draftAimAssistEnabled != previousAimAssistEnabled)
                dirty = true;

            const float previousAimAssistStrength = draftAimAssistStrength;
            ImGui::BeginDisabled(!draftAimAssistEnabled);
            ImGui::SliderFloat(
                "Aim Assist Strength", &draftAimAssistStrength, k_minAimAssistStrength, k_maxAimAssistStrength, "%.2f");
            ImGui::EndDisabled();
            draftAimAssistStrength = std::clamp(draftAimAssistStrength, k_minAimAssistStrength, k_maxAimAssistStrength);
            if (draftAimAssistStrength != previousAimAssistStrength)
                dirty = true;

            ImGui::Spacing();
            const bool previousShowControllerBindings = draftShowControllerBindings;
            ImGui::Checkbox("Controller Bindings", &draftShowControllerBindings);
            if (draftShowControllerBindings != previousShowControllerBindings) {
                listeningBinding.reset();
                dirty = true;
            }

            ImGui::Spacing();
            const BindingDevice visibleDevice =
                draftShowControllerBindings ? BindingDevice::Controller : BindingDevice::KeyboardMouse;
            if (ImGui::BeginTable("bindings", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                ImGui::TableSetupColumn("Alt Binding", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                ImGui::TableHeadersRow();

                for (Action action : InputBindings::actions()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(InputBindings::actionLabel(action).data());

                    for (std::size_t slot = 0; slot < InputBindings::kBindingSlots; ++slot) {
                        ImGui::TableSetColumnIndex(static_cast<int>(slot + 1));
                        const bool listening = listeningBinding && listeningBinding->action == action &&
                                               listeningBinding->device == visibleDevice &&
                                               listeningBinding->slot == slot;
                        const std::string label =
                            listening ? std::string(draftShowControllerBindings ? "Press controller input"
                                                                                : "Press key, mouse, or wheel")
                                      : InputBindings::bindingLabel(draftBindings.get(action, visibleDevice, slot));
                        ImGui::PushID(static_cast<int>(action));
                        ImGui::PushID(static_cast<int>(slot));
                        if (ImGui::Button(label.c_str(), {-1.0f, 0.0f})) {
                            listeningBinding =
                                ListeningBinding{.action = action, .device = visibleDevice, .slot = slot};
                            statusMessage.clear();
                        }
                        ImGui::PopID();
                        ImGui::PopID();
                    }
                }

                ImGui::EndTable();
            }

            if (!statusMessage.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", statusMessage.c_str());
            }

            ImGui::Spacing();
            if (ImGui::Button("Reset to Defaults", {buttonWidth, 30.0f})) {
                const UserSettings defaults;
                draftBindings = defaults.inputBindings;
                draftMouseSensitivity = defaults.mouseSensitivity;
                draftHorizontalFovDegrees = defaults.horizontalFovDegrees;
                draftShowControllerBindings = defaults.showControllerBindings;
                draftGamepadYawSensitivity = defaults.gamepadYawSensitivity;
                draftGamepadPitchSensitivity = defaults.gamepadPitchSensitivity;
                draftGamepadLookDeadzone = defaults.gamepadLookDeadzone;
                draftGamepadMoveDeadzone = defaults.gamepadMoveDeadzone;
                draftAimAssistEnabled = defaults.aimAssistEnabled;
                draftAimAssistStrength = defaults.aimAssistStrength;
                draftGamepadSwapSticks = defaults.gamepadSwapSticks;
                listeningBinding.reset();
                dirty = true;
                statusMessage.clear();
            }

            const float halfWidth = (buttonWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Apply", {halfWidth, 34.0f})) {
                settings.inputBindings = draftBindings;
                settings.mouseSensitivity = draftMouseSensitivity;
                settings.horizontalFovDegrees = draftHorizontalFovDegrees;
                settings.showControllerBindings = draftShowControllerBindings;
                settings.gamepadYawSensitivity = draftGamepadYawSensitivity;
                settings.gamepadPitchSensitivity = draftGamepadPitchSensitivity;
                settings.gamepadLookDeadzone = draftGamepadLookDeadzone;
                settings.gamepadMoveDeadzone = draftGamepadMoveDeadzone;
                settings.aimAssistEnabled = draftAimAssistEnabled;
                settings.aimAssistStrength = draftAimAssistStrength;
                settings.gamepadSwapSticks = draftGamepadSwapSticks;
                const bool saved = user_settings::save(std::string(settingsPath), settings);
                statusMessage = saved ? "Settings saved." : "Settings could not be saved.";
                dirty = false;
                listeningBinding.reset();
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
            draftHorizontalFovDegrees = settings.horizontalFovDegrees;
            draftShowControllerBindings = settings.showControllerBindings;
            draftGamepadYawSensitivity = settings.gamepadYawSensitivity;
            draftGamepadPitchSensitivity = settings.gamepadPitchSensitivity;
            draftGamepadLookDeadzone = settings.gamepadLookDeadzone;
            draftGamepadMoveDeadzone = settings.gamepadMoveDeadzone;
            draftAimAssistEnabled = settings.aimAssistEnabled;
            draftAimAssistStrength = settings.aimAssistStrength;
            draftGamepadSwapSticks = settings.gamepadSwapSticks;
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
