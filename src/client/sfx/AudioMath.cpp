/// @file AudioMath.cpp
/// @brief Pure spatial-audio helper implementation.

#include "AudioMath.hpp"

#include <algorithm>
#include <cmath>

namespace audio
{

float distanceAttenuation(float distance, float fullGainDistance, float silentDistance) noexcept
{
    if (silentDistance <= fullGainDistance)
        return distance <= fullGainDistance ? 1.0f : 0.0f;
    if (distance <= fullGainDistance)
        return 1.0f;
    if (distance >= silentDistance)
        return 0.0f;

    const float t = (distance - fullGainDistance) / (silentDistance - fullGainDistance);
    const float smooth = t * t * (3.0f - 2.0f * t);
    return 1.0f - smooth;
}

float dopplerRatio(const glm::vec3& sourceToListener,
                   const glm::vec3& sourceVelocity,
                   const glm::vec3& listenerVelocity) noexcept
{
    const float distance = glm::length(sourceToListener);
    if (distance <= 0.001f)
        return 1.0f;

    const glm::vec3 dir = sourceToListener / distance;
    const float listenerAlong = glm::dot(listenerVelocity, dir);
    const float sourceAlong = glm::dot(sourceVelocity, dir);
    const float c = k_speedOfSoundUnitsPerSecond;
    const float numerator = std::clamp(c + listenerAlong, c * 0.25f, c * 1.75f);
    const float denominator = std::clamp(c + sourceAlong, c * 0.25f, c * 1.75f);
    return std::clamp(numerator / denominator, 0.65f, 1.55f);
}

SpatialParams evaluateSpatial(const glm::vec3& sourcePosition,
                              const glm::vec3& sourceVelocity,
                              const ListenerState& listener,
                              bool occluded) noexcept
{
    SpatialParams params;
    const glm::vec3 toSource = sourcePosition - listener.position;
    const float distance = glm::length(toSource);
    params.gain = distanceAttenuation(distance);
    params.audible = params.gain > 0.0005f;
    if (!params.audible) {
        params.gain = 0.0f;
        return params;
    }

    glm::vec3 forward = glm::dot(listener.forward, listener.forward) > 0.0001f ? glm::normalize(listener.forward)
                                                                               : glm::vec3{0.0f, 0.0f, 1.0f};
    glm::vec3 up =
        glm::dot(listener.up, listener.up) > 0.0001f ? glm::normalize(listener.up) : glm::vec3{0.0f, 1.0f, 0.0f};
    glm::vec3 right = glm::cross(forward, up);
    if (glm::dot(right, right) <= 0.0001f)
        right = glm::vec3{1.0f, 0.0f, 0.0f};
    else
        right = glm::normalize(right);

    const glm::vec3 dir = distance > 0.001f ? toSource / distance : forward;
    const float pan = std::clamp(glm::dot(dir, right), -1.0f, 1.0f);
    const float angle = (pan + 1.0f) * 0.25f * 3.1415926535f;
    params.left = std::cos(angle);
    params.right = std::sin(angle);
    params.dopplerRatio = dopplerRatio(-toSource, sourceVelocity, listener.velocity);

    if (occluded) {
        params.gain *= 0.62f;
        params.lowPass = 0.34f;
        params.reverbSend = 0.23f;
    } else {
        params.lowPass = 1.0f;
        params.reverbSend =
            std::clamp((distance - k_fullGainDistance) / (k_silentDistance - k_fullGainDistance), 0.0f, 0.18f);
    }

    return params;
}

} // namespace audio
