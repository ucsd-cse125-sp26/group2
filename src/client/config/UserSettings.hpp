#pragma once

#include "InputBindings.hpp"

#include <string>

struct UserSettings
{
    InputBindings inputBindings{InputBindings::defaults()};
    float mouseSensitivity{0.0007f};
};

namespace user_settings
{
std::string getPath();
UserSettings load(const std::string& path);
bool save(const std::string& path, const UserSettings& settings);
} // namespace user_settings
