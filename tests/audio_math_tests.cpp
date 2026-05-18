#include "client/sfx/AudioMath.hpp"

#include <cmath>
#include <cstdlib>

namespace
{
void require(bool condition)
{
    if (!condition)
        std::abort();
}

void testAttenuation()
{
    require(audio::distanceAttenuation(0.0f) == 1.0f);
    require(audio::distanceAttenuation(audio::k_fullGainDistance) == 1.0f);
    require(audio::distanceAttenuation(audio::k_silentDistance) == 0.0f);
    const float mid = audio::distanceAttenuation((audio::k_fullGainDistance + audio::k_silentDistance) * 0.5f);
    require(mid > 0.0f && mid < 1.0f);
}

void testPanAndOcclusion()
{
    audio::ListenerState listener;
    listener.position = {0.0f, 0.0f, 0.0f};
    listener.forward = {0.0f, 0.0f, 1.0f};
    listener.up = {0.0f, 1.0f, 0.0f};

    const auto right = audio::evaluateSpatial({100.0f, 0.0f, 1000.0f}, {}, listener, false);
    require(right.audible);
    require(right.right > right.left);

    const auto occluded = audio::evaluateSpatial({0.0f, 0.0f, 1000.0f}, {}, listener, true);
    const auto clear = audio::evaluateSpatial({0.0f, 0.0f, 1000.0f}, {}, listener, false);
    require(occluded.gain < clear.gain);
    require(occluded.lowPass < clear.lowPass);
    require(occluded.reverbSend > clear.reverbSend);
}

void testDopplerClamp()
{
    audio::ListenerState listener;
    listener.velocity = {0.0f, 0.0f, 9000.0f};
    const auto params = audio::evaluateSpatial({0.0f, 0.0f, 500.0f}, {0.0f, 0.0f, -9000.0f}, listener, false);
    require(params.dopplerRatio >= 0.65f);
    require(params.dopplerRatio <= 1.55f);
    require(std::isfinite(params.dopplerRatio));
}
} // namespace

int main()
{
    testAttenuation();
    testPanAndOcclusion();
    testDopplerClamp();
    return 0;
}
