/// @file AudioMath.hpp
/// @brief Pure helpers for spatial audio attenuation, panning, Doppler, and occlusion.

#pragma once

#include <glm/glm.hpp>

namespace audio
{

inline constexpr int k_mixerSampleRate = 48000;
inline constexpr int k_mixerChannels = 2;
inline constexpr float k_speedOfSoundUnitsPerSecond = 13500.0f;
inline constexpr float k_fullGainDistance = 450.0f;
inline constexpr float k_silentDistance = 3500.0f;

struct ListenerState
{
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 velocity{0.0f};
};

struct SpatialParams
{
    float gain = 1.0f;
    float left = 0.70710678f;
    float right = 0.70710678f;
    float dopplerRatio = 1.0f;
    float lowPass = 1.0f;
    float reverbSend = 0.0f;
    bool audible = true;
};

[[nodiscard]] float distanceAttenuation(float distance,
                                        float fullGainDistance = k_fullGainDistance,
                                        float silentDistance = k_silentDistance) noexcept;
[[nodiscard]] float dopplerRatio(const glm::vec3& sourceToListener,
                                 const glm::vec3& sourceVelocity,
                                 const glm::vec3& listenerVelocity) noexcept;
[[nodiscard]] SpatialParams evaluateSpatial(const glm::vec3& sourcePosition,
                                            const glm::vec3& sourceVelocity,
                                            const ListenerState& listener,
                                            bool occluded,
                                            float fullGainDistance = k_fullGainDistance,
                                            float silentDistance = k_silentDistance) noexcept;

} // namespace audio
