#include "RegistrySerialization.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponState.hpp"
#include "entt/entity/fwd.hpp"
#include "network/RegistryArchive.hpp"

#include <entt/entt.hpp>
#include <tuple>
#include <vector>

namespace
{

template <typename S, typename A, typename... Cs>
void getAll(S& snapshot, A& archive, std::tuple<Cs...>*)
{
    (snapshot.template get<Cs>(archive), ...);
}

} // namespace

namespace registry_serialization
{

// NOTE: this is where any component that should be sent to clients must be listed.
// The order of components in this tuple is the order they will be serialized in.
using Synced = std::tuple<entt::entity, Position, Velocity, PlayerState, CollisionShape, WeaponState>;

std::vector<uint8_t> serialize(const entt::registry& registry)
{
    OutputArchive archive;

    auto snapshot = entt::snapshot{registry};

    getAll(snapshot, archive, static_cast<Synced*>(nullptr));

    return std::move(archive.buffer);
}

void Loader::apply(const uint8_t* data, size_t size)
{
    InputArchive archive(data, size);

    getAll(loader, archive, static_cast<Synced*>(nullptr));

    // Not sure if we need this:
    loader.orphans();
}

entt::entity Loader::map(entt::entity e) const
{
    return loader.map(e);
}

} // namespace registry_serialization
