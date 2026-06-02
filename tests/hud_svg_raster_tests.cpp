#include "client/hud/HudSvgRaster.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>

namespace
{

bool hasVisibleAlpha(std::span<const unsigned char> rgba)
{
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
        if (rgba[i] > 0)
            return true;
    }
    return false;
}

bool hasAntialiasAlpha(std::span<const unsigned char> rgba)
{
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
        if (rgba[i] > 0 && rgba[i] < 255)
            return true;
    }
    return false;
}

bool checkSvg(const std::filesystem::path& assetDir, const char* name, int width, int height)
{
    const HudSvgBitmap bitmap = rasterizeHudSvg(assetDir / name, width, height);
    if (bitmap.empty()) {
        std::cerr << name << " did not rasterize\n";
        return false;
    }
    if (bitmap.width != width || bitmap.height != height) {
        std::cerr << name << " rasterized at unexpected size " << bitmap.width << "x" << bitmap.height << "\n";
        return false;
    }
    if (!hasVisibleAlpha(bitmap.rgba)) {
        std::cerr << name << " has no visible alpha\n";
        return false;
    }
    if (!hasAntialiasAlpha(bitmap.rgba)) {
        std::cerr << name << " has no intermediate alpha edge pixels\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    const std::filesystem::path assetDir = std::filesystem::path(GROUP2_SOURCE_DIR) / "assets" / "hud_icons";

    bool ok = true;
    ok = checkSvg(assetDir, "Radar.svg", 192, 192) && ok;
    ok = checkSvg(assetDir, "tactical.svg", 96, 96) && ok;
    ok = checkSvg(assetDir, "HealthFrameBack.svg", 512, 48) && ok;
    ok = checkSvg(assetDir, "HealthFrameFront.svg", 512, 48) && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

