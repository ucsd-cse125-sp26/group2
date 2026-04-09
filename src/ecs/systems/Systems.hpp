#pragma once

#include "ecs/registry/Registry.hpp"

/// @brief ECS systems namespace.
///
/// Each free function (or callable) represents one system. A system receives the
/// shared Registry and any per-frame state it needs, then queries and mutates components.
///
/// Concrete system implementations live in individual `.hpp`/`.cpp` pairs under this directory.
namespace systems
{

/// @brief Placeholder system — replace with real logic.
inline void update(Registry& /*registry*/, float /*dt*/) {}

} // namespace systems
