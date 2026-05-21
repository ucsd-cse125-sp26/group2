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
constexpr float k_minMouseSensitivity = 0.0001f;
constexpr float k_maxMouseSensitivity = 0.0050f;
constexpr float k_minFovDegrees = 50.0f;
constexpr float k_maxFovDegrees = 120.0f;

std::string altKey(std::string_view key)
{
    std::string alt(key);
    alt += "-alt";
    return alt;
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
        settings.mouseSensitivity = std::clamp(*v, k_minMouseSensitivity, k_maxMouseSensitivity);
    }
    if (auto v = input["fov-degrees"].value<float>()) {
        settings.fovDegrees = std::clamp(*v, k_minFovDegrees, k_maxFovDegrees);
    }
    if (auto v = input["show-controller-bindings"].value<bool>()) {
        settings.showControllerBindings = *v;
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
    out << "mouse-sensitivity = " << std::clamp(settings.mouseSensitivity, k_minMouseSensitivity, k_maxMouseSensitivity)
        << "\n";
    out << "fov-degrees = " << std::clamp(settings.fovDegrees, k_minFovDegrees, k_maxFovDegrees) << "\n";
    out << "show-controller-bindings = " << (settings.showControllerBindings ? "true" : "false") << "\n";

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
