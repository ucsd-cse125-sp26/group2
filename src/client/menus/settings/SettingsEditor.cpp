#include "SettingsEditor.hpp"

#include "menus/MenuTheme.hpp"

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <imgui.h>

namespace
{
constexpr float k_minFovDegrees = 50.0f;
constexpr float k_maxFovDegrees = 120.0f;
constexpr float k_minGamepadSensitivity = 1.0f;
constexpr float k_maxGamepadSensitivity = 10.0f;
constexpr float k_minGamepadLookDeadzone = 0.0f;
constexpr float k_maxGamepadLookDeadzone = 0.4f;
constexpr float k_minGamepadMoveDeadzone = 0.0f;
constexpr float k_maxGamepadMoveDeadzone = 0.5f;
constexpr float k_minAimAssistStrength = 0.0f;
constexpr float k_maxAimAssistStrength = 1.0f;

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

void SettingsEditor::open(const UserSettings& settings)
{
    open_ = true;
    resetDraft(settings);
    dirty_ = false;
    listeningBinding_.reset();
    statusMessage_.clear();
}

void SettingsEditor::close()
{
    closeEditor();
}

bool SettingsEditor::isOpen() const
{
    return open_;
}

bool SettingsEditor::handleEscape(const UserSettings& settings)
{
    if (confirm_.isOpen()) {
        confirm_.cancel();
        return false;
    }
    if (listeningBinding_) {
        listeningBinding_.reset();
        return false;
    }
    return requestClose(settings);
}

bool SettingsEditor::consumeEvent(const SDL_Event& event)
{
    if (!open_)
        return false;

    if (listeningBinding_) {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            if (event.key.key == SDLK_ESCAPE) {
                listeningBinding_.reset();
            } else if (event.key.key == SDLK_BACKSPACE || event.key.key == SDLK_DELETE) {
                draftBindings_.rebind(
                    listeningBinding_->action, Binding::unbound(), listeningBinding_->device, listeningBinding_->slot);
                listeningBinding_.reset();
                dirty_ = true;
            } else if (listeningBinding_->device == BindingDevice::KeyboardMouse) {
                draftBindings_.rebind(listeningBinding_->action,
                                      Binding::bindKeyboard(event.key.scancode),
                                      listeningBinding_->device,
                                      listeningBinding_->slot);
                listeningBinding_.reset();
                dirty_ = true;
            }
            return true;
        }

        const std::optional<Binding> candidate = listeningBinding_->device == BindingDevice::Controller
                                                     ? controllerBindingFromEvent(event)
                                                     : keyboardMouseBindingFromEvent(event);
        if (candidate) {
            draftBindings_.rebind(
                listeningBinding_->action, *candidate, listeningBinding_->device, listeningBinding_->slot);
            listeningBinding_.reset();
            dirty_ = true;
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

SettingsEditorResult SettingsEditor::render(UserSettings& settings, std::string_view settingsPath, float uiScale)
{
    SettingsEditorResult result{};
    if (!open_)
        return result;

    menu_theme::terminalStatusLine(dirty_ ? "SETTINGS DIRTY" : "SETTINGS CLEAN", "TAB TO CHANGE SECTION");
    menu_theme::terminalSection("SETTINGS");

    if (ImGui::BeginTabBar("SettingsTabs")) {
        if (ImGui::BeginTabItem("General")) {
            activeTab_ = Tab::General;
            renderGeneralTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Keyboard & Mouse")) {
            activeTab_ = Tab::KeyboardMouse;
            renderKeyboardMouseTab(uiScale);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controller")) {
            activeTab_ = Tab::Controller;
            renderControllerTab(uiScale);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (!statusMessage_.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", statusMessage_.c_str());
    }

    ImGui::Spacing();
    const float buttonWidth = ImGui::GetContentRegionAvail().x;
    if (menu_theme::terminalActionRow("RESET TO DEFAULTS", nullptr, {buttonWidth, 30.0f * uiScale})) {
        resetToDefaults();
    }

    if (menu_theme::terminalActionRow("APPLY", "save current settings", {buttonWidth, 34.0f * uiScale})) {
        apply(settings, settingsPath, result);
    }
    if (menu_theme::terminalActionRow("BACK", "leave settings", {buttonWidth, 34.0f * uiScale})) {
        result.closeRequested = requestClose(settings);
    }
    if (menu_theme::terminalActionRow("CANCEL", "discard prompt if needed", {buttonWidth, 34.0f * uiScale}, true)) {
        result.closeRequested = requestClose(settings);
    }

    if (dirty_) {
        ImGui::TextUnformatted("Unsaved changes");
    }

    const ConfirmResult confirmResult = confirm_.drawAndPoll();
    if (confirmResult == ConfirmResult::Confirmed) {
        resetDraft(settings);
        closeEditor();
        result.closeRequested = true;
    }

    return result;
}

void SettingsEditor::resetDraft(const UserSettings& settings)
{
    draftBindings_ = settings.inputBindings;
    draftMouseSensitivity_ = settings.mouseSensitivity;
    draftHorizontalFovDegrees_ = settings.horizontalFovDegrees;
    draftShowControllerBindings_ = settings.showControllerBindings;
    activeTab_ = draftShowControllerBindings_ ? Tab::Controller : Tab::General;
    draftGamepadYawSensitivity_ = settings.gamepadYawSensitivity;
    draftGamepadPitchSensitivity_ = settings.gamepadPitchSensitivity;
    draftGamepadLookDeadzone_ = settings.gamepadLookDeadzone;
    draftGamepadMoveDeadzone_ = settings.gamepadMoveDeadzone;
    draftAimAssistEnabled_ = settings.aimAssistEnabled;
    draftAimAssistStrength_ = settings.aimAssistStrength;
    draftGamepadSwapSticks_ = settings.gamepadSwapSticks;
    draftMuzzleFlashEnabled_ = settings.muzzleFlashEnabled;
}

void SettingsEditor::closeEditor()
{
    open_ = false;
    listeningBinding_.reset();
    statusMessage_.clear();
    dirty_ = false;
    confirm_.cancel();
}

bool SettingsEditor::requestClose(const UserSettings& settings)
{
    if (dirty_) {
        requestDiscardConfirm();
        return false;
    }

    resetDraft(settings);
    closeEditor();
    return true;
}

void SettingsEditor::requestDiscardConfirm()
{
    listeningBinding_.reset();
    confirm_.open({.title = "Discard Settings?",
                   .message = "You have unsaved settings changes. Discard them?",
                   .confirmText = "Discard",
                   .cancelText = "Keep Editing",
                   .confirmIsDanger = true});
}

void SettingsEditor::apply(UserSettings& settings, std::string_view settingsPath, SettingsEditorResult& result)
{
    settings.inputBindings = draftBindings_;
    settings.mouseSensitivity = draftMouseSensitivity_;
    settings.horizontalFovDegrees = draftHorizontalFovDegrees_;
    settings.showControllerBindings = activeTab_ == Tab::Controller || draftShowControllerBindings_;
    settings.gamepadYawSensitivity = draftGamepadYawSensitivity_;
    settings.gamepadPitchSensitivity = draftGamepadPitchSensitivity_;
    settings.gamepadLookDeadzone = draftGamepadLookDeadzone_;
    settings.gamepadMoveDeadzone = draftGamepadMoveDeadzone_;
    settings.aimAssistEnabled = draftAimAssistEnabled_;
    settings.aimAssistStrength = draftAimAssistStrength_;
    settings.gamepadSwapSticks = draftGamepadSwapSticks_;
    settings.muzzleFlashEnabled = draftMuzzleFlashEnabled_;

    const bool saved = user_settings::save(std::string(settingsPath), settings);
    statusMessage_ = saved ? "Settings saved." : "Settings could not be saved.";
    dirty_ = false;
    listeningBinding_.reset();
    result.applied = true;
}

void SettingsEditor::resetToDefaults()
{
    const UserSettings defaults;
    resetDraft(defaults);
    dirty_ = true;
    listeningBinding_.reset();
    statusMessage_.clear();
}

void SettingsEditor::renderGeneralTab()
{
    ImGui::Spacing();

    const float previousFov = draftHorizontalFovDegrees_;
    ImGui::SliderFloat("Horizontal FOV", &draftHorizontalFovDegrees_, k_minFovDegrees, k_maxFovDegrees, "%.0f");
    draftHorizontalFovDegrees_ = std::clamp(draftHorizontalFovDegrees_, k_minFovDegrees, k_maxFovDegrees);
    if (draftHorizontalFovDegrees_ != previousFov)
        dirty_ = true;

    const bool previousMuzzleFlash = draftMuzzleFlashEnabled_;
    ImGui::Checkbox("Muzzle Flash", &draftMuzzleFlashEnabled_);
    if (draftMuzzleFlashEnabled_ != previousMuzzleFlash)
        dirty_ = true;
}

void SettingsEditor::renderKeyboardMouseTab(float uiScale)
{
    ImGui::Spacing();

    const float previousSensitivity = draftMouseSensitivity_;
    float displayedMouseSensitivity = draftMouseSensitivity_ * user_settings::kMouseSensitivityDisplayScale;
    ImGui::SliderFloat("Mouse Sensitivity",
                       &displayedMouseSensitivity,
                       user_settings::kMinMouseSensitivity * user_settings::kMouseSensitivityDisplayScale,
                       user_settings::kMaxMouseSensitivity * user_settings::kMouseSensitivityDisplayScale,
                       "%.3f");
    draftMouseSensitivity_ = displayedMouseSensitivity / user_settings::kMouseSensitivityDisplayScale;
    draftMouseSensitivity_ =
        std::clamp(draftMouseSensitivity_, user_settings::kMinMouseSensitivity, user_settings::kMaxMouseSensitivity);
    if (draftMouseSensitivity_ != previousSensitivity)
        dirty_ = true;

    ImGui::Spacing();
    renderBindingsTable(BindingDevice::KeyboardMouse, uiScale);
}

void SettingsEditor::renderControllerTab(float uiScale)
{
    ImGui::Spacing();
    draftShowControllerBindings_ = true;

    const float previousGamepadYawSensitivity = draftGamepadYawSensitivity_;
    ImGui::SliderFloat("Horizontal Look Sensitivity",
                       &draftGamepadYawSensitivity_,
                       k_minGamepadSensitivity,
                       k_maxGamepadSensitivity,
                       "%.1f rad/s");
    draftGamepadYawSensitivity_ =
        std::clamp(draftGamepadYawSensitivity_, k_minGamepadSensitivity, k_maxGamepadSensitivity);
    if (draftGamepadYawSensitivity_ != previousGamepadYawSensitivity)
        dirty_ = true;

    const float previousGamepadPitchSensitivity = draftGamepadPitchSensitivity_;
    ImGui::SliderFloat("Vertical Look Sensitivity",
                       &draftGamepadPitchSensitivity_,
                       k_minGamepadSensitivity,
                       k_maxGamepadSensitivity,
                       "%.1f rad/s");
    draftGamepadPitchSensitivity_ =
        std::clamp(draftGamepadPitchSensitivity_, k_minGamepadSensitivity, k_maxGamepadSensitivity);
    if (draftGamepadPitchSensitivity_ != previousGamepadPitchSensitivity)
        dirty_ = true;

    const float previousGamepadLookDeadzone = draftGamepadLookDeadzone_;
    ImGui::SliderFloat(
        "Right Stick Deadzone", &draftGamepadLookDeadzone_, k_minGamepadLookDeadzone, k_maxGamepadLookDeadzone, "%.2f");
    draftGamepadLookDeadzone_ =
        std::clamp(draftGamepadLookDeadzone_, k_minGamepadLookDeadzone, k_maxGamepadLookDeadzone);
    if (draftGamepadLookDeadzone_ != previousGamepadLookDeadzone)
        dirty_ = true;

    const float previousGamepadMoveDeadzone = draftGamepadMoveDeadzone_;
    ImGui::SliderFloat(
        "Left Stick Deadzone", &draftGamepadMoveDeadzone_, k_minGamepadMoveDeadzone, k_maxGamepadMoveDeadzone, "%.2f");
    draftGamepadMoveDeadzone_ =
        std::clamp(draftGamepadMoveDeadzone_, k_minGamepadMoveDeadzone, k_maxGamepadMoveDeadzone);
    if (draftGamepadMoveDeadzone_ != previousGamepadMoveDeadzone)
        dirty_ = true;

    const bool previousGamepadSwapSticks = draftGamepadSwapSticks_;
    ImGui::Checkbox("Swap Sticks (southpaw)", &draftGamepadSwapSticks_);
    if (draftGamepadSwapSticks_ != previousGamepadSwapSticks)
        dirty_ = true;

    const bool previousAimAssistEnabled = draftAimAssistEnabled_;
    ImGui::Checkbox("Aim Assist", &draftAimAssistEnabled_);
    if (draftAimAssistEnabled_ != previousAimAssistEnabled)
        dirty_ = true;

    const float previousAimAssistStrength = draftAimAssistStrength_;
    ImGui::BeginDisabled(!draftAimAssistEnabled_);
    ImGui::SliderFloat(
        "Aim Assist Strength", &draftAimAssistStrength_, k_minAimAssistStrength, k_maxAimAssistStrength, "%.2f");
    ImGui::EndDisabled();
    draftAimAssistStrength_ = std::clamp(draftAimAssistStrength_, k_minAimAssistStrength, k_maxAimAssistStrength);
    if (draftAimAssistStrength_ != previousAimAssistStrength)
        dirty_ = true;

    ImGui::Spacing();
    renderBindingsTable(BindingDevice::Controller, uiScale);
}

void SettingsEditor::renderBindingsTable(BindingDevice device, float uiScale)
{
    if (!ImGui::BeginTable("bindings", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
        return;

    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthFixed, 190.0f * uiScale);
    ImGui::TableSetupColumn("Alt Binding", ImGuiTableColumnFlags_WidthFixed, 190.0f * uiScale);
    ImGui::TableHeadersRow();

    for (Action action : InputBindings::actions()) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(InputBindings::actionLabel(action).data());

        for (std::size_t slot = 0; slot < InputBindings::kBindingSlots; ++slot) {
            ImGui::TableSetColumnIndex(static_cast<int>(slot + 1));
            const bool listening = listeningBinding_ && listeningBinding_->action == action &&
                                   listeningBinding_->device == device && listeningBinding_->slot == slot;
            const std::string label =
                listening ? std::string(device == BindingDevice::Controller ? "Press controller input"
                                                                            : "Press key, mouse, or wheel")
                          : InputBindings::bindingLabel(draftBindings_.get(action, device, slot));
            ImGui::PushID(static_cast<int>(device));
            ImGui::PushID(static_cast<int>(action));
            ImGui::PushID(static_cast<int>(slot));
            if (ImGui::Button(label.c_str(), {-1.0f, 0.0f})) {
                listeningBinding_ = ListeningBinding{.action = action, .device = device, .slot = slot};
                statusMessage_.clear();
            }
            ImGui::PopID();
            ImGui::PopID();
            ImGui::PopID();
        }
    }

    ImGui::EndTable();
}
