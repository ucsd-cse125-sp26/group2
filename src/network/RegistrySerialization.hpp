#pragma once

#include "ecs/components/InputSnapshot.hpp"
#include "entt/entity/fwd.hpp"

#include <entt/entt.hpp>
#include <optional>
#include <vector>

namespace registry_serialization
{

struct RemoteInputRecord
{
    entt::entity entity{entt::null};
    InputSnapshot input{};
};

std::vector<uint8_t> serialize(const entt::registry& registry);

class Loader
{
public:
    explicit Loader(entt ::registry& registry) : registry(registry), loader(registry) {}
    void apply(const uint8_t* data, size_t size, std::optional<entt::entity> localPlayerServerEntity = std::nullopt);
    [[nodiscard]] entt::entity map(entt::entity) const;

private:
    entt::registry& registry;
    entt::continuous_loader loader;
};

} // namespace registry_serialization
