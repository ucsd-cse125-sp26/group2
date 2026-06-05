#include "UserSettings.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <toml++/toml.hpp>

namespace user_settings
{
namespace
{
constexpr float k_minFovDegrees = 70.0f;
constexpr float k_maxFovDegrees = 120.0f;
constexpr float k_minGamepadSensitivity = 1.0f;
constexpr float k_maxGamepadSensitivity = 10.0f;
constexpr float k_minGamepadLookDeadzone = 0.0f;
constexpr float k_maxGamepadLookDeadzone = 0.4f;
constexpr float k_minGamepadMoveDeadzone = 0.0f;
constexpr float k_maxGamepadMoveDeadzone = 0.5f;
constexpr float k_minAimAssistStrength = 0.0f;
constexpr float k_maxAimAssistStrength = 1.0f;
constexpr float k_minVolume = 0.0f;
constexpr float k_maxVolume = 1.0f;

std::string altKey(std::string_view key)
{
    std::string alt(key);
    alt += "-alt";
    return alt;
}

std::string tomlString(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char c : value) {
        switch (c) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(c);
            break;
        }
    }
    escaped.push_back('"');
    return escaped;
}
} // namespace

std::string getPath()
{
    char* prefPath = SDL_GetPrefPath("ucsd-cse125", "group2");
    if (!prefPath) {
        SDL_Log("Could not resolve user settings directory: %s", SDL_GetError());
        return "user-settings.toml";
    }

    std::string path = std::string(prefPath) + "user-settings.toml";
    SDL_free(prefPath);
    return path;
}

UserSettings load(const std::string& path)
{
    UserSettings settings;
    if (!std::filesystem::exists(path)) {
        return settings;
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        SDL_Log("Could not load user settings '%s' (%s); using defaults.", path.c_str(), e.description().data());
        return settings;
    }

    const toml::node_view input = tbl["input"] ? tbl["input"] : tbl["user"];
    if (auto v = input["mouse-sensitivity"].value<float>()) {
        settings.mouseSensitivity = std::clamp(*v, kMinMouseSensitivity, kMaxMouseSensitivity);
    }
    if (auto v = input["horizontal-fov-degrees"].value<float>()) {
        settings.horizontalFovDegrees = std::clamp(*v, k_minFovDegrees, k_maxFovDegrees);
    }
    if (auto v = input["show-controller-bindings"].value<bool>()) {
        settings.showControllerBindings = *v;
    }
    if (auto v = input["gamepad-yaw-sensitivity"].value<float>()) {
        settings.gamepadYawSensitivity = std::clamp(*v, k_minGamepadSensitivity, k_maxGamepadSensitivity);
    }
    if (auto v = input["gamepad-pitch-sensitivity"].value<float>()) {
        settings.gamepadPitchSensitivity = std::clamp(*v, k_minGamepadSensitivity, k_maxGamepadSensitivity);
    }
    if (auto v = input["gamepad-look-deadzone"].value<float>()) {
        settings.gamepadLookDeadzone = std::clamp(*v, k_minGamepadLookDeadzone, k_maxGamepadLookDeadzone);
    }
    if (auto v = input["gamepad-move-deadzone"].value<float>()) {
        settings.gamepadMoveDeadzone = std::clamp(*v, k_minGamepadMoveDeadzone, k_maxGamepadMoveDeadzone);
    }
    if (auto v = input["aim-assist-enabled"].value<bool>()) {
        settings.aimAssistEnabled = *v;
    }
    if (auto v = input["aim-assist-strength"].value<float>()) {
        settings.aimAssistStrength = std::clamp(*v, k_minAimAssistStrength, k_maxAimAssistStrength);
    }
    if (auto v = input["gamepad-swap-sticks"].value<bool>()) {
        settings.gamepadSwapSticks = *v;
    }
    if (auto v = input["muzzle-flash"].value<bool>()) {
        settings.muzzleFlashEnabled = *v;
    }
    const toml::node_view audio = tbl["audio"] ? tbl["audio"] : input;
    if (auto v = audio["music-volume"].value<float>()) {
        settings.musicVolume = std::clamp(*v, k_minVolume, k_maxVolume);
    }
    if (auto v = audio["sfx-volume"].value<float>()) {
        settings.sfxVolume = std::clamp(*v, k_minVolume, k_maxVolume);
    }
    if (auto v = audio["output-device"].value<std::string>()) {
        settings.audioOutputDeviceName = *v;
    }
    if (auto v = audio["input-device"].value<std::string>()) {
        settings.audioInputDeviceName = *v;
    }

    auto loadBindingTable = [&](const auto table, BindingDevice device) {
        if (!table)
            return;

        for (Action action : InputBindings::actions()) {
            const std::string key(InputBindings::configKey(action));
            const std::string keyAlt = altKey(key);
            for (std::size_t slot = 0; slot < InputBindings::kBindingSlots; ++slot) {
                const std::string& slotKey = slot == 0 ? key : keyAlt;
                if (auto value = table[slotKey].template value<std::string>()) {
                    Binding binding;
                    if (InputBindings::bindingFromConfigValue(*value, binding)) {
                        settings.inputBindings.rebind(action, binding, device, slot);
                    } else {
                        SDL_Log("Invalid binding '%s' for '%s' in '%s'; using default.",
                                value->c_str(),
                                slotKey.c_str(),
                                path.c_str());
                    }
                }
            }
        }
    };

    const toml::node_view keyboard = input["keyboard"];
    const toml::node_view controller = input["controller"];
    loadBindingTable(keyboard ? keyboard : input, BindingDevice::KeyboardMouse);
    loadBindingTable(controller, BindingDevice::Controller);

    return settings;
}

bool save(const std::string& path, const UserSettings& settings)
{
    std::ofstream out(path);
    if (!out) {
        SDL_Log("Could not save user settings '%s'.", path.c_str());
        return false;
    }

    out << "[input]\n";
    out << "mouse-sensitivity = " << std::clamp(settings.mouseSensitivity, kMinMouseSensitivity, kMaxMouseSensitivity)
        << "\n";
    out << "horizontal-fov-degrees = " << std::clamp(settings.horizontalFovDegrees, k_minFovDegrees, k_maxFovDegrees)
        << "\n";
    out << "show-controller-bindings = " << (settings.showControllerBindings ? "true" : "false") << "\n";
    out << "gamepad-yaw-sensitivity = "
        << std::clamp(settings.gamepadYawSensitivity, k_minGamepadSensitivity, k_maxGamepadSensitivity) << "\n";
    out << "gamepad-pitch-sensitivity = "
        << std::clamp(settings.gamepadPitchSensitivity, k_minGamepadSensitivity, k_maxGamepadSensitivity) << "\n";
    out << "gamepad-look-deadzone = "
        << std::clamp(settings.gamepadLookDeadzone, k_minGamepadLookDeadzone, k_maxGamepadLookDeadzone) << "\n";
    out << "gamepad-move-deadzone = "
        << std::clamp(settings.gamepadMoveDeadzone, k_minGamepadMoveDeadzone, k_maxGamepadMoveDeadzone) << "\n";
    out << "aim-assist-enabled = " << (settings.aimAssistEnabled ? "true" : "false") << "\n";
    out << "aim-assist-strength = "
        << std::clamp(settings.aimAssistStrength, k_minAimAssistStrength, k_maxAimAssistStrength) << "\n";
    out << "gamepad-swap-sticks = " << (settings.gamepadSwapSticks ? "true" : "false") << "\n";
    out << "muzzle-flash = " << (settings.muzzleFlashEnabled ? "true" : "false") << "\n";

    out << "\n[audio]\n";
    out << "music-volume = " << std::clamp(settings.musicVolume, k_minVolume, k_maxVolume) << "\n";
    out << "sfx-volume = " << std::clamp(settings.sfxVolume, k_minVolume, k_maxVolume) << "\n";
    out << "output-device = " << tomlString(settings.audioOutputDeviceName) << "\n";
    out << "input-device = " << tomlString(settings.audioInputDeviceName) << "\n";

    out << "\n[input.keyboard]\n";
    for (Action action : InputBindings::actions()) {
        const std::string key(InputBindings::configKey(action));
        out << key << " = \"" << InputBindings::bindingConfigValue(settings.inputBindings.get(action)) << "\"\n";
        out << altKey(key) << " = \""
            << InputBindings::bindingConfigValue(settings.inputBindings.get(action, BindingDevice::KeyboardMouse, 1))
            << "\"\n";
    }

    out << "\n[input.controller]\n";
    for (Action action : InputBindings::actions()) {
        const std::string key(InputBindings::configKey(action));
        out << key << " = \""
            << InputBindings::bindingConfigValue(settings.inputBindings.get(action, BindingDevice::Controller))
            << "\"\n";
        out << altKey(key) << " = \""
            << InputBindings::bindingConfigValue(settings.inputBindings.get(action, BindingDevice::Controller, 1))
            << "\"\n";
    }
    return true;
}
} // namespace user_settings
