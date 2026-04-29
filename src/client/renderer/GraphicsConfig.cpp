/// @file GraphicsConfig.cpp
/// @brief Implementation of loadGraphicsConfig().

#include "GraphicsConfig.hpp"

#include <cstdio>
#include <cstring>
#include <toml++/toml.hpp>

namespace
{
GpuBackend defaultBackend()
{
#if defined(_WIN32)
    return GpuBackend::Direct3D12;
#elif defined(__APPLE__)
    return GpuBackend::Metal;
#else
    return GpuBackend::Vulkan;
#endif
}

GpuBackend parseBackend(std::string_view s)
{
    if (s == "auto")
        return GpuBackend::Auto;
    if (s == "direct3d12" || s == "d3d12" || s == "dx12")
        return GpuBackend::Direct3D12;
    if (s == "vulkan")
        return GpuBackend::Vulkan;
    if (s == "metal")
        return GpuBackend::Metal;
    std::fprintf(stderr, "[config] Warning: unknown graphics.backend '%.*s' — using platform default.\n",
                 static_cast<int>(s.size()), s.data());
    return defaultBackend();
}
} // namespace

GraphicsConfig loadGraphicsConfig(const char* path)
{
    GraphicsConfig cfg;
    cfg.backend = defaultBackend();

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        std::fprintf(stderr,
                     "[config] Warning: could not load '%s' (%s) — using graphics defaults.\n",
                     path,
                     e.description().data());
        return cfg;
    }

    auto graphics = tbl["graphics"];
    if (auto v = graphics["backend"].value<std::string>())
        cfg.backend = parseBackend(*v);

    return cfg;
}

const char* gpuBackendHintString(GpuBackend backend)
{
    switch (backend) {
    case GpuBackend::Direct3D12:
        return "direct3d12";
    case GpuBackend::Vulkan:
        return "vulkan";
    case GpuBackend::Metal:
        return "metal";
    case GpuBackend::Auto:
    default:
        return nullptr;
    }
}
