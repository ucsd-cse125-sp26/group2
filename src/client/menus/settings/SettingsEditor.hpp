#pragma once

#include "config/InputBindings.hpp"
#include "config/UserSettings.hpp"
#include "menus/pause/ConfirmModal.hpp"

#include <SDL3/SDL_events.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

/// @brief Result commands emitted by one settings-editor render pass.
struct SettingsEditorResult
{
    bool applied = false;        ///< Settings were applied and saved this frame.
    bool closeRequested = false; ///< Caller should leave the settings page.
};

/// @brief Shared tabbed settings editor used by front-end screens and the pause menu.
class SettingsEditor
{
public:
    /// @brief Open the editor with a fresh draft copied from live settings.
    void open(const UserSettings& settings);

    /// @brief Close the editor and discard transient UI state.
    void close();

    /// @brief True while the settings editor is active.
    [[nodiscard]] bool isOpen() const;

    /// @brief Handle Escape according to the current listening/dirty/modal state.
    /// @return True when the caller should close the settings page.
    bool handleEscape(const UserSettings& settings);

    /// @brief Consume input events while the editor owns menu focus.
    bool consumeEvent(const SDL_Event& event);

    /// @brief Draw the tabbed editor inside the current ImGui window.
    SettingsEditorResult render(UserSettings& settings, std::string_view settingsPath, float uiScale);

private:
    enum class Tab
    {
        General,
        Audio,
        KeyboardMouse,
        Controller,
    };

    struct ListeningBinding
    {
        Action action{Action::Forward};
        BindingDevice device{BindingDevice::KeyboardMouse};
        std::size_t slot{0};
    };

    void resetDraft(const UserSettings& settings);
    void closeEditor();
    bool requestClose(const UserSettings& settings);
    void requestDiscardConfirm();
    void apply(UserSettings& settings, std::string_view settingsPath, SettingsEditorResult& result);
    void resetToDefaults(UserSettings& settings);
    void restoreOriginalAudioSettings(UserSettings& settings);
    void updateLiveAudioSettings(UserSettings& settings);
    void renderGeneralTab();
    void renderAudioTab(UserSettings& settings);
    void renderKeyboardMouseTab(float uiScale);
    void renderControllerTab(float uiScale);
    void renderBindingsTable(BindingDevice device, float uiScale);

    bool open_ = false;
    Tab activeTab_ = Tab::General;
    InputBindings draftBindings_ = InputBindings::defaults();
    float draftMouseSensitivity_ = user_settings::kDefaultMouseSensitivity;
    float draftHorizontalFovDegrees_ = 110.0f;
    bool draftShowControllerBindings_ = false;
    float draftGamepadYawSensitivity_ = 6.0f;
    float draftGamepadPitchSensitivity_ = 6.0f;
    float draftGamepadLookDeadzone_ = 0.0f;
    float draftGamepadMoveDeadzone_ = 0.0f;
    bool draftAimAssistEnabled_ = true;
    float draftAimAssistStrength_ = 1.0f;
    bool draftGamepadSwapSticks_ = false;
    bool draftMuzzleFlashEnabled_ = true;
    float draftMusicVolume_ = 0.7f;
    float draftSfxVolume_ = 1.0f;
    std::string draftAudioOutputDeviceName_;
    std::string draftAudioInputDeviceName_;
    float originalMusicVolume_ = 0.7f;
    float originalSfxVolume_ = 1.0f;
    std::string originalAudioOutputDeviceName_;
    std::string originalAudioInputDeviceName_;
    bool dirty_ = false;
    std::optional<ListeningBinding> listeningBinding_;
    std::string statusMessage_;
    ConfirmModal confirm_;
};
