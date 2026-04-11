#pragma once

/// @brief Tag component for world entities that continuously emit smoke/fire/steam particles.
enum class EmitterType : uint8_t
{
    Smoke,
    Fire,
    Steam
};

struct ParticleEmitterTag
{
    EmitterType type = EmitterType::Smoke;
    float ratePerSecond = 8.f;
    float accumulator = 0.f;
    float radius = 40.f; ///< Spawn radius around entity position.
};
