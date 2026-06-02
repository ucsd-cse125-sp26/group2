/// @file HudSvgRaster.hpp
/// @brief CPU SVG raster helpers used by the HUD SVG atlas and tests.

#pragma once

#include "HudTypes.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

struct HudSvgBitmap
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;

    [[nodiscard]] bool empty() const { return width <= 0 || height <= 0 || rgba.empty(); }
};

[[nodiscard]] std::string_view hudSvgFilename(HudIcon id);
[[nodiscard]] HudIcon hudIconFromFilename(std::string_view filename);
[[nodiscard]] HudSvgBitmap rasterizeHudSvg(const std::filesystem::path& path, int width, int height);

