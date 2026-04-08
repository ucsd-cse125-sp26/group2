#pragma once

#include "ecs/registry/Registry.hpp"

// ---------------------------------------------------------------------------
// Systems — ECS system stubs.
//
// Each free function (or callable) represents one system.  A system receives
// the shared Registry and any other per-frame state it needs, then queries /
// mutates components.
//
// Add concrete system implementations here or split them into individual
// .hpp / .cpp pairs under this directory as the game grows.
// ---------------------------------------------------------------------------

namespace systems
{

// Placeholder — replace with real system logic.
inline void update(Registry& /*registry*/, float /*dt*/) {}

} // namespace systems
