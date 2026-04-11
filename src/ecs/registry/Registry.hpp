/// @file Registry.hpp
/// @brief Shared ECS registry type alias for the game engine.

#pragma once

#include <entt/entt.hpp>

/// @brief Shared ECS registry type alias.
///
/// Uses `entt::registry` directly. EnTT is a required dependency.
using Registry = entt::registry;
