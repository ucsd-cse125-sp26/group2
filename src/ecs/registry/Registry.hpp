#pragma once

// ---------------------------------------------------------------------------
// Registry — ECS registry type.
//
// Toggle at configure time:
//   cmake --preset debug -DUSE_ENTT=ON   → uses entt::registry directly
//   cmake --preset debug                  → stub class (roll your own)
//
// The stub is intentionally minimal — add whatever interface your ECS needs.
// ---------------------------------------------------------------------------

#ifdef USE_ENTT
#include <entt/entt.hpp>
using Registry = entt::registry;
#else
// Stub registry — replace with your own implementation.
class Registry
{};
#endif
