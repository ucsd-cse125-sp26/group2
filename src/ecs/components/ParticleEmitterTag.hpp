/// @file ParticleEmitterTag.hpp
/// @brief Particle emitter tag component for environmental effects.

#pragma once

/// @brief Type of environmental particles emitted by an entity.
enum class EmitterType : uint8_t
{
    Smoke, ///< Slow-rising dark smoke puffs.
    Fire,  ///< Bright flickering fire particles.
    Steam  ///< Light translucent steam wisps.
};

/// @brief Tag component for world entities that continuously emit smoke/fire/steam particles.
struct ParticleEmitterTag
{
    EmitterType type = EmitterType::Smoke; ///< Kind of particle effect to emit.
    float ratePerSecond = 8.f;             ///< Target emission rate (particles/s).
    float accumulator = 0.f;               ///< Fractional particle accumulator for sub-frame emission.
    float radius = 40.f;                   ///< Spawn radius around entity position.
};
