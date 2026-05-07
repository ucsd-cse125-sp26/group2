/// @file GraphicsConfig.hpp
/// @brief Graphics configuration loaded from config.toml at startup.

#pragma once

#include <string>

/// @brief Selected GPU backend / driver.
///
/// Maps to SDL3's `SDL_HINT_GPU_DRIVER` values when set explicitly.  `Auto`
/// leaves the hint unset and lets SDL pick the best available backend for the
/// platform.
enum class GpuBackend
{
    Auto,
    Direct3D12,
    Vulkan,
    Metal,
};

struct GraphicsConfig
{
    /// Preferred backend.  Defaults to platform best:
    ///   Windows → Direct3D12, macOS → Metal, Linux → Vulkan.
    GpuBackend backend = GpuBackend::Auto;
};

[[nodiscard]] GraphicsConfig loadGraphicsConfig(const char* path);

/// @brief Map GpuBackend to the string SDL expects in SDL_HINT_GPU_DRIVER.
/// Returns nullptr for GpuBackend::Auto (do not set the hint).
[[nodiscard]] const char* gpuBackendHintString(GpuBackend backend);
