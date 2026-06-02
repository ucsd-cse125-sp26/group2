/// @file HudSvgRaster.cpp
/// @brief LunaSVG-backed CPU rasterization for HUD SVG assets.

#include "HudSvgRaster.hpp"

#include <lunasvg.h>

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

namespace
{

struct HudSvgName
{
    HudIcon id;
    std::string_view filename;
};

constexpr HudSvgName kHudSvgNames[] = {
    {HudIcon::AbilityBarBack, "AbilityBarBack.svg"},
    {HudIcon::AbilityBarFront, "AbilityBarFront.svg"},
    {HudIcon::AbilityIconFrame, "AbilityIconFrame.svg"},
    {HudIcon::BulletCountBox, "BulletCountBox.svg"},
    {HudIcon::GrenadeBox, "GrenadeBox.svg"},
    {HudIcon::HealthFrameBack, "HealthFrameBack.svg"},
    {HudIcon::HealthFrameFront, "HealthFrameFront.svg"},
    {HudIcon::LetterButton, "LetterButton.svg"},
    {HudIcon::LevelBarBack, "LevelBarBack.svg"},
    {HudIcon::LevelBarFront, "LevelBarFront.svg"},
    {HudIcon::Radar, "Radar.svg"},
    {HudIcon::Scoreboard, "Scoreboard.svg"},
    {HudIcon::EnemyDiamond, "enemy_diamond.svg"},
    {HudIcon::Fall, "fall.svg"},
    {HudIcon::Grapple, "grapple.svg"},
    {HudIcon::Gravity, "gravity.svg"},
    {HudIcon::Grenade, "grenade.svg"},
    {HudIcon::Headshot, "headshot.svg"},
    {HudIcon::Hp, "hp.svg"},
    {HudIcon::PlayerArrow, "player_arrow.svg"},
    {HudIcon::Shield, "shield.svg"},
    {HudIcon::Skull, "skull.svg"},
    {HudIcon::Tactical, "tactical.svg"},
};

void replaceAll(std::string& text, std::string_view from, std::string_view to)
{
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string loadAndNormalizeSvg(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};

    std::string svg((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    replaceAll(svg, "currentColor", "#ffffff");
    return svg;
}

} // namespace

std::string_view hudSvgFilename(HudIcon id)
{
    for (const HudSvgName& entry : kHudSvgNames) {
        if (entry.id == id)
            return entry.filename;
    }
    return {};
}

HudIcon hudIconFromFilename(std::string_view filename)
{
    for (const HudSvgName& entry : kHudSvgNames) {
        if (entry.filename == filename)
            return entry.id;
    }
    return HudIcon::None;
}

HudSvgBitmap rasterizeHudSvg(const std::filesystem::path& path, int width, int height)
{
    HudSvgBitmap out;
    out.width = std::max(1, width);
    out.height = std::max(1, height);

    std::string svg = loadAndNormalizeSvg(path);
    if (svg.empty()) {
        SDL_Log("HudSvgRaster: failed to read %s", path.string().c_str());
        return {};
    }

    std::unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(svg);
    if (!document) {
        SDL_Log("HudSvgRaster: failed to parse %s", path.string().c_str());
        return {};
    }

    lunasvg::Bitmap bitmap = document->renderToBitmap(out.width, out.height, 0x00000000);
    if (bitmap.isNull()) {
        SDL_Log("HudSvgRaster: failed to render %s at %dx%d", path.string().c_str(), out.width, out.height);
        return {};
    }

    bitmap.convertToRGBA();

    out.rgba.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4u);
    const unsigned char* src = bitmap.data();
    const int srcStride = bitmap.stride();
    for (int y = 0; y < out.height; ++y) {
        const unsigned char* srcRow = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(srcStride);
        unsigned char* dstRow = out.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) * 4u;
        std::copy(srcRow, srcRow + static_cast<std::size_t>(out.width) * 4u, dstRow);
    }

    return out;
}

