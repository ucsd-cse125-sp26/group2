#include "ecs/AssetCatalog.hpp"

#include <cassert>
#include <cmath>
#include <cstring>

namespace
{

const AssetDefinition* findProp(const char* name)
{
    for (const AssetDefinition& def : kPropAssets) {
        if (std::strcmp(def.name, name) == 0)
            return &def;
    }
    return nullptr;
}

bool nearlyEqual(float a, float b)
{
    return std::fabs(a - b) < 0.001f;
}

} // namespace

int main()
{
    static_assert(kPropAssets.size() == 2, "Suzanne comparison needs both source assets registered");

    const AssetDefinition* usdz = findProp("suzanne_substance_usdz");
    const AssetDefinition* glb = findProp("suzanne_substance_glb");

    assert(usdz != nullptr);
    assert(glb != nullptr);

    assert(std::strcmp(usdz->filename, "suzanne_substance.usdz") == 0);
    assert(std::strcmp(glb->filename, "suzanne_substance_v2.glb") == 0);

    assert(usdz->role == AssetRole::Prop);
    assert(glb->role == AssetRole::Prop);

    assert(!usdz->loadCollision);
    assert(!glb->loadCollision);

    assert(nearlyEqual(usdz->loadTranslation.y, glb->loadTranslation.y));
    assert(nearlyEqual(usdz->loadTranslation.z, glb->loadTranslation.z));
    assert(usdz->loadTranslation.x < glb->loadTranslation.x);

    return 0;
}
