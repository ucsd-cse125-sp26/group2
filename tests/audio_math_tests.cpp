#include "client/sfx/AudioMath.hpp"

#include <cassert>
#include <cmath>

namespace
{
void testAttenuation()
{
    assert(audio::distanceAttenuation(0.0f) == 1.0f);
    assert(audio::distanceAttenuation(audio::k_fullGainDistance) == 1.0f);
    assert(audio::distanceAttenuation(audio::k_silentDistance) == 0.0f);
    const float mid = audio::distanceAttenuation((audio::k_fullGainDistance + audio::k_silentDistance) * 0.5f);
    assert(mid > 0.0f && mid < 1.0f);
}

void testPanAndOcclusion()
{
    audio::ListenerState listener;
    listener.position = {0.0f, 0.0f, 0.0f};
    listener.forward = {0.0f, 0.0f, 1.0f};
    listener.up = {0.0f, 1.0f, 0.0f};

    const auto right = audio::evaluateSpatial({100.0f, 0.0f, 1000.0f}, {}, listener, false);
    assert(right.audible);
    assert(right.right > right.left);

    const auto occluded = audio::evaluateSpatial({0.0f, 0.0f, 1000.0f}, {}, listener, true);
    const auto clear = audio::evaluateSpatial({0.0f, 0.0f, 1000.0f}, {}, listener, false);
    assert(occluded.gain < clear.gain);
    assert(occluded.lowPass < clear.lowPass);
    assert(occluded.reverbSend > clear.reverbSend);
}

void testDopplerClamp()
{
    audio::ListenerState listener;
    listener.velocity = {0.0f, 0.0f, 9000.0f};
    const auto params = audio::evaluateSpatial({0.0f, 0.0f, 500.0f}, {0.0f, 0.0f, -9000.0f}, listener, false);
    assert(params.dopplerRatio >= 0.65f);
    assert(params.dopplerRatio <= 1.55f);
    assert(std::isfinite(params.dopplerRatio));
}
} // namespace

int main()
{
    testAttenuation();
    testPanAndOcclusion();
    testDopplerClamp();
    return 0;
}
