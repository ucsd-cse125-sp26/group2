#include "ecs/components/BeamState.hpp"
#include "particles/effects/EnergyTeslaArcEffect.hpp"

#include <cassert>
#include <type_traits>

namespace
{

float distanceToLine(glm::vec3 p, glm::vec3 a, glm::vec3 b)
{
    const glm::vec3 ab = b - a;
    const float denom = glm::dot(ab, ab);
    if (denom <= 0.0001f)
        return glm::length(p - a);
    const float t = glm::clamp(glm::dot(p - a, ab) / denom, 0.0f, 1.0f);
    return glm::length(p - (a + ab * t));
}

} // namespace

int main()
{
    static_assert(std::is_trivially_copyable_v<BeamState>);
    static_assert(std::is_trivially_copyable_v<ArcVertex>);

    BeamState unlocked{};
    unlocked.active = true;
    unlocked.locked = 0;
    unlocked.lockStrength = 0.0f;
    unlocked.origin = {0.0f, 0.0f, 0.0f};
    unlocked.hitPoint = {100.0f, 0.0f, 0.0f};
    unlocked.guidePoint = unlocked.hitPoint;
    assert(unlocked.locked == 0);
    assert(unlocked.lockStrength == 0.0f);
    assert(glm::length(unlocked.guidePoint - unlocked.hitPoint) < 0.001f);

    BeamState locked{};
    locked.active = true;
    locked.locked = 1;
    locked.lockStrength = 1.0f;
    locked.origin = {0.0f, 0.0f, 0.0f};
    locked.guidePoint = {100.0f, 0.0f, 0.0f};
    locked.hitPoint = {68.0f, 34.0f, 0.0f};
    assert(locked.locked == 1);
    assert(glm::length(locked.guidePoint - locked.hitPoint) > 1.0f);

    const auto straight =
        EnergyTeslaArcEffect::buildGuidePathForTest(unlocked.origin, unlocked.guidePoint, locked.hitPoint, false, 0.0f);
    assert(straight.count == 33);
    assert(glm::length(straight.points[0] - unlocked.origin) < 0.001f);
    assert(glm::length(straight.points[straight.count - 1] - unlocked.guidePoint) < 0.001f);
    for (uint32_t i = 0; i < straight.count; ++i)
        assert(distanceToLine(straight.points[i], unlocked.origin, unlocked.guidePoint) < 0.001f);

    const auto bent =
        EnergyTeslaArcEffect::buildGuidePathForTest(locked.origin, locked.guidePoint, locked.hitPoint, true, 1.0f);
    assert(glm::length(bent.points[0] - locked.origin) < 0.001f);
    assert(glm::length(bent.points[bent.count - 1] - locked.hitPoint) < 0.001f);
    for (uint32_t i = 0; i < bent.count / 2; ++i)
        assert(distanceToLine(bent.points[i], locked.origin, locked.guidePoint) < 0.001f);
    assert(distanceToLine(bent.points[bent.count - 3], locked.origin, locked.guidePoint) > 8.0f);

    const auto newlyLocked =
        EnergyTeslaArcEffect::buildGuidePathForTest(locked.origin, locked.guidePoint, locked.hitPoint, true, 0.01f);
    assert(glm::length(newlyLocked.points[newlyLocked.count - 1] - locked.hitPoint) < 0.001f);
    assert(distanceToLine(newlyLocked.points[newlyLocked.count - 3], locked.origin, locked.guidePoint) > 8.0f);

    EnergyTeslaArcEffect effect;
    effect.drive(42, locked.origin, locked.guidePoint, locked.hitPoint, true, 1.0f);
    effect.update(0.016f, {0.0f, 0.0f, -1.0f});
    assert(effect.activeBeamCount() == 1);
    assert(effect.mainArcCount() > 0);
    assert(effect.arcCount() > effect.mainArcCount());
    assert(effect.arcCount() < 8192);

    effect.update(0.016f, {0.0f, 0.0f, -1.0f});
    assert(effect.activeBeamCount() == 0);
    assert(effect.arcCount() == 0);

    return 0;
}
