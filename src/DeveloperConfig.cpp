/// @file DeveloperConfig.cpp
/// @brief Implementation of loadDeveloperConfig().

#include "DeveloperConfig.hpp"

#include <cstdio>
#include <toml++/toml.hpp>

DeveloperConfig loadDeveloperConfig(const char* path)
{
    DeveloperConfig cfg;

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        std::fprintf(stderr,
                     "[config] Warning: could not load '%s' (%s) -- using developer defaults.\n",
                     path,
                     e.description().data());
        return cfg;
    }

    auto developer = tbl["developer"];
    if (auto v = developer["skip_lobby"].value<bool>())
        cfg.skipLobby = *v;

    if (auto v = developer["voice_capture"].value<bool>())
        cfg.voiceCapture = *v;

    return cfg;
}
