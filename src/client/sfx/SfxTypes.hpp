/// @file SfxTypes.hpp
/// @brief Sound effect identifiers, categories, and the SoundClip data type.

#pragma once

#include <SDL3/SDL_audio.h>

#include <cstddef>
#include <cstdint>
#include <vector>

/// @brief Identifies a loaded sound clip.
enum class SfxId : uint8_t
{
    // Weapons
    RifleFire,     ///< Voicy_Charge Rifle SFX.mp3
    RocketFire,    ///< Voicy_Minecraft TNT Explosion.mp3
    RailGunFire,   ///< Voicy_Charge Rifle SFX.mp3
    EnergyGunFire, ///< Voicy_Charge Rifle SFX.mp3

    // Impacts / hitmarkers
    FleshHit,  ///< Voicy_Flesh Bullet Impact SFX.mp3
    Headshot,  ///< Voicy_Headshot Rapid SFX.mp3
    Explosion, ///< Voicy_Minecraft TNT Explosion.mp3

    // Player feedback
    DamageTaken, ///< Voicy_roblox ooof.mp3
    ArmorBreak,  ///< Voicy_Fortnite Shield Break.mp3
    Death,       ///< Voicy_Minecraft Death Sound.mp3
    Respawn,     ///< Voicy_totem of undying sfx .mp3
    KillConfirm, ///< Voicy_Pilot Killed Indicator SFX.mp3

    // Charge rifle
    ChargeRifleLoad,  ///< charge-rifle-load.wav (play once when charge starts)
    ChargeRifleShoot, ///< charge-rifle-shoot.wav (play on release)

    // Energy beam
    EnergyBeamLoop, ///< Voicy_Thunderstruck into.mp3 (play while beam active)

    // Healing / Shield
    Healing,        ///< Voicy_Syringe SFX .mp3
    ShieldRecharge, ///< Voicy_Halo Shield Recharge.mp3

    // Movement / equipment placeholders. These synthesize if final assets are absent.
    FootstepLight,
    FootstepHeavy,
    GrenadeThrow,
    VoiceStart,
    VoiceStop,

    _Count
};

/// @brief Sound category for per-category volume control.
enum class SfxCategory : uint8_t
{
    Weapons,
    Impacts,
    Player,
    Footsteps,
    Voice,
    UI,
    _Count
};

/// @brief A decoded sound clip ready for playback.
struct SoundClip
{
    std::vector<uint8_t> pcmData; ///< Original/intermediate PCM bytes while loading.
    std::vector<float> samples;   ///< Mixer-ready F32 stereo samples.
    SDL_AudioSpec spec{};         ///< Format descriptor matching pcmData, or mixer format after conversion.
    float durationSeconds = 0.0f; ///< Total playback duration in seconds.
    std::size_t frameCount = 0;   ///< Number of interleaved stereo frames in samples.
    SfxCategory category = SfxCategory::Weapons;
    float defaultGain = 1.0f;     ///< Base volume for this clip.
    float minCooldown = 0.05f;    ///< Minimum seconds between repeated plays.
    bool loaded = false;          ///< True when the clip was decoded successfully.
};
