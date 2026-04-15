#include "RegistrySerialization.hpp"

#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "entt/entity/fwd.hpp"
#include "network/RegistryArchive.hpp"

#include <entt/entt.hpp>
#include <vector>

static_assert(std::is_trivially_copyable_v<Position>);
static_assert(std::is_trivially_copyable_v<Velocity>);
static_assert(std::is_trivially_copyable_v<PlayerState>);

std::vector<uint8_t> registry_serialization::serialize(const entt::registry& registry)
{
    OutputArchive archive;

    // clang-format off
    entt::snapshot{registry}
        .get<entt::entity>(archive)
        .get<Position>(archive)
        .get<Velocity>(archive)
        .get<PlayerState>( archive);
    // clang-format on
    return std::move(archive.buffer);
}

void registry_serialization::Loader::apply(const uint8_t* data, size_t size)
{
    InputArchive archive(data, size);

    // clang-format off
    loader
        .get<entt::entity>(archive)
        .get<Position>(archive)
        .get<Velocity>(archive)
        .get<PlayerState>( archive);
    // clang-format on

    // loader.orphans;
}

entt::entity registry_serialization::Loader::map(entt::entity e) const
{
    return loader.map(e);
}
