#pragma once

#include "entt/entity/fwd.hpp"

#include <entt/entt.hpp>
#include <vector>

namespace registry_serialization
{

std::vector<uint8_t> serialize(const entt::registry& registry);

class Loader
{
public:
    explicit Loader(entt ::registry& registry) : loader(registry) {}
    void apply(const uint8_t* data, size_t size);
    [[nodiscard]] entt::entity map(entt::entity) const;

private:
    entt::continuous_loader loader;
};

} // namespace registry_serialization
