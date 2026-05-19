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
std::string getPath()
{
    char* prefPath = SDL_GetPrefPath("cse125", "group2");
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

    auto user = tbl["user"];
    if (auto v = user["mouse-sensitivity"].value<float>()) {
        settings.mouseSensitivity = std::clamp(*v, 0.0001f, 0.0200f);
    }

    return settings;
}

bool save(const std::string& path, const UserSettings& settings)
{
    std::ofstream out(path);
    if (!out) {
        SDL_Log("Could not save user settings '%s'.", path.c_str());
        return false;
    }

    out << "[user]\n";
    out << "mouse-sensitivity = " << settings.mouseSensitivity << "\n";
    return true;
}
} // namespace user_settings
