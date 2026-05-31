#include "client/particles/effects/TracerEffect.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

float segmentLength(const TracerParticle& tracer)
{
    const glm::vec3 d = tracer.tip - tracer.tail;
    return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}

bool testRifleTracerStartsAsSmallProjectile()
{
    TracerEffect tracers;
    tracers.spawnRifleTracer({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 1000.0f);

    bool ok = true;
    ok &= expect(tracers.count() == 1, "rifle tracer should spawn one visible particle");

    const TracerParticle& p = tracers.data()[0];
    ok &= expect(p.tip.x >= 50.0f && p.tip.x <= 110.0f, "rifle tracer should spawn forward from the muzzle");
    ok &= expect(p.tail.x >= 0.0f, "rifle tracer tail should not extend behind the muzzle on spawn");
    ok &= expect(segmentLength(p) >= 50.0f && segmentLength(p) <= 120.0f,
                 "rifle tracer should spawn as a readable short projectile");
    ok &= expect(p.radius >= 2.0f && p.radius <= 3.5f, "rifle tracer radius should be visible but not chunky");
    return ok;
}

bool testRifleTracerMovesForwardWithReadableTail()
{
    TracerEffect tracers;
    Registry registry;
    tracers.spawnRifleTracer({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 1000.0f);
    const float startTipX = tracers.data()[0].tip.x;

    tracers.update(1.0f / 120.0f, registry);

    bool ok = true;
    ok &= expect(tracers.count() == 1, "rifle tracer should still be alive after one visual step");

    const TracerParticle& p = tracers.data()[0];
    ok &= expect(p.tip.x > startTipX, "rifle tracer tip should move forward over time");
    ok &= expect(p.tail.x > 0.0f, "rifle tracer tail should move away from the muzzle after the first step");
    ok &= expect(segmentLength(p) >= 100.0f && segmentLength(p) <= 170.0f,
                 "rifle tracer tail should be readable while moving");
    return ok;
}

bool testRifleTracerOverlapsRifleFireCadence()
{
    TracerEffect tracers;
    Registry registry;
    tracers.spawnRifleTracer({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 1000.0f);

    tracers.update(0.1f, registry);

    bool ok = true;
    ok &= expect(tracers.count() == 1, "rifle tracer should last into the next rifle shot window");

    tracers.spawnRifleTracer({0.0f, 3.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 1000.0f);
    ok &= expect(tracers.count() == 2, "continuous rifle fire should show overlapping tracer projectiles");
    return ok;
}

bool testRifleTracerExpiresAfterPassingRange()
{
    TracerEffect tracers;
    Registry registry;
    tracers.spawnRifleTracer({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 120.0f);

    tracers.update(0.05f, registry);

    return expect(tracers.count() == 0, "rifle tracer should expire after passing its visual range");
}
} // namespace

int main()
{
    bool ok = true;
    ok &= testRifleTracerStartsAsSmallProjectile();
    ok &= testRifleTracerMovesForwardWithReadableTail();
    ok &= testRifleTracerOverlapsRifleFireCadence();
    ok &= testRifleTracerExpiresAfterPassingRange();
    return ok ? 0 : 1;
}
