/// @file SfxTypes.hpp
/// @brief Sound effect identifiers, categories, and the SoundClip data type.

#pragma once

#include <SDL3/SDL_audio.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

/// @brief Identifies a loaded sound clip.
enum class SfxId : uint8_t
{
    // Weapons
    RifleFire,     ///< Weapons/Rifle/Rifle_Shooting.wav
    RocketFire,    ///< Weapons/Rocket/Rocket_Shooting.wav
    RailGunFire,   ///< Weapons/Railgun/Railgun_Shooting.wav
    EnergyGunFire, ///< Weapons/EnergyWeapon/Energy_Shooting_Start.wav
    ShotgunFire,   ///< Weapons/Shotgun/Shotgun_Shooting.wav
    RifleReload,   ///< Weapons/Rifle/Rifle_Reloading.wav
    RocketReload,  ///< Weapons/Rocket/Rocket_Reloading.wav
    RailGunReload, ///< Weapons/Railgun/Railgun_Reloading.wav
    EnergyReload,  ///< Weapons/EnergyWeapon/Energy_Reloading.wav
    ShotgunReload, ///< Weapons/Shotgun/Shotgun_Reloading.wav
    RailGunCharge, ///< Weapons/Railgun/Railgun_Charge.wav

    // Impacts / hitmarkers
    FleshHit,         ///< Voicy_Flesh Bullet Impact SFX.mp3
    Headshot,         ///< Voicy_Headshot Rapid SFX.mp3
    Explosion,        ///< Generic explosion fallback.
    RocketExplosion,  ///< Weapons/Rocket/Explosion_Rocket.wav
    MolotovExplosion, ///< Weapons/Grenade/Explosion_Molotov.wav
    HEExplosion,      ///< Weapons/Grenade/Explosion_HE.wav

    // Player feedback
    DamageTaken, ///< damage.mp3
    ArmorBreak,  ///< Voicy_Fortnite Shield Break.mp3
    Death,       ///< Death.wav
    KillConfirm, ///< Voicy_Pilot Killed Indicator SFX.mp3

    // Charge rifle
    ChargeRifleLoad,  ///< Legacy charge cue alias.
    ChargeRifleShoot, ///< charge-rifle-shoot.wav (play on release)

    // Energy beam
    EnergyBeamLoop, ///< Weapons/EnergyWeapon/Energy_Shooting.wav (play while beam active)

    // Healing / pickups
    Healing,                 ///< Health.wav
    PowerupPickup,           ///< Generic powerup pickup fallback.
    DamagePowerupPickup,     ///< damage_powerup.mp3
    OvershieldPowerupPickup, ///< overshield_powerup.mp3

    // Movement / equipment placeholders. FootstepLight/Heavy still back simple impact fallbacks.
    FootstepLight,
    FootstepHeavy,
    ConcreteFootstep01,
    ConcreteFootstep02,
    ConcreteFootstep03,
    ConcreteFootstep04,
    ConcreteFootstep05,
    ConcreteFootstep06,
    ConcreteFootstep07,
    ConcreteFootstep08,
    ConcreteFootstep09,
    ConcreteFootstep10,
    ConcreteFootstep11,
    ConcreteFootstep12,
    ConcreteFootstep13,
    ConcreteFootstep14,
    ConcreteFootstep15,
    ConcreteFootstep16,
    ConcreteFootstep17,
    DirtFootstep01,
    DirtFootstep02,
    DirtFootstep03,
    DirtFootstep04,
    DirtFootstep05,
    DirtFootstep06,
    DirtFootstep07,
    DirtFootstep08,
    DirtFootstep09,
    DirtFootstep10,
    DirtFootstep11,
    DirtFootstep12,
    DirtFootstep13,
    DirtFootstep14,
    Slide,          ///< sliding.mp3 — looped while in Slide mode.
    DashSfx,        ///< Synth: dash whoosh.
    GravityFlipSfx, ///< Synth: gravity flip warble.
    GrappleSfx,     ///< Synth: grapple twang/launch.
    RecallSfx,      ///< Synth: recall rewind.
    GrenadeThrow,
    VoiceStart,
    VoiceStop,
    MenuMusic, ///< Music/Gamesong1.wav
    GameMusic, ///< Music/Gamesong2.wav
    GameMusicLoop, ///< Music/Gamesong2_Main2.wav

    // Menu UI feedback. Files live flat under assets/sounds/MenuSFX/.
    UiHover01,
    UiHover02,
    UiConfirm01,
    UiConfirm02,
    UiBack01,
    UiToggle01,
    UiSliderStep01,
    UiModal01,
    UiSuccess01,
    UiError01,
    UiError02,
    UiDisabled01,
    UiDanger01,

    _Count
};

/// @brief Semantic menu/UI sound actions.
enum class UiSoundAction : uint8_t
{
    Hover,
    Confirm,
    Back,
    Toggle,
    SliderStep,
    ModalOpen,
    ModalClose,
    Success,
    Error,
    Disabled,
    Danger,
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
    Music,
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

inline const char* sfxIdName(SfxId id) noexcept
{
    switch (id) {
    case SfxId::RifleFire:
        return "RifleFire";
    case SfxId::RocketFire:
        return "RocketFire";
    case SfxId::RailGunFire:
        return "RailGunFire";
    case SfxId::EnergyGunFire:
        return "EnergyGunFire";
    case SfxId::ShotgunFire:
        return "ShotgunFire";
    case SfxId::RifleReload:
        return "RifleReload";
    case SfxId::RocketReload:
        return "RocketReload";
    case SfxId::RailGunReload:
        return "RailGunReload";
    case SfxId::EnergyReload:
        return "EnergyReload";
    case SfxId::ShotgunReload:
        return "ShotgunReload";
    case SfxId::RailGunCharge:
        return "RailGunCharge";
    case SfxId::FleshHit:
        return "FleshHit";
    case SfxId::Headshot:
        return "Headshot";
    case SfxId::Explosion:
        return "Explosion";
    case SfxId::RocketExplosion:
        return "RocketExplosion";
    case SfxId::MolotovExplosion:
        return "MolotovExplosion";
    case SfxId::HEExplosion:
        return "HEExplosion";
    case SfxId::DamageTaken:
        return "DamageTaken";
    case SfxId::ArmorBreak:
        return "ArmorBreak";
    case SfxId::Death:
        return "Death";
    case SfxId::KillConfirm:
        return "KillConfirm";
    case SfxId::ChargeRifleLoad:
        return "ChargeRifleLoad";
    case SfxId::ChargeRifleShoot:
        return "ChargeRifleShoot";
    case SfxId::EnergyBeamLoop:
        return "EnergyBeamLoop";
    case SfxId::Healing:
        return "Healing";
    case SfxId::PowerupPickup:
        return "PowerupPickup";
    case SfxId::DamagePowerupPickup:
        return "DamagePowerupPickup";
    case SfxId::OvershieldPowerupPickup:
        return "OvershieldPowerupPickup";
    case SfxId::FootstepLight:
        return "FootstepLight";
    case SfxId::FootstepHeavy:
        return "FootstepHeavy";
    case SfxId::ConcreteFootstep01:
        return "ConcreteFootstep01";
    case SfxId::ConcreteFootstep02:
        return "ConcreteFootstep02";
    case SfxId::ConcreteFootstep03:
        return "ConcreteFootstep03";
    case SfxId::ConcreteFootstep04:
        return "ConcreteFootstep04";
    case SfxId::ConcreteFootstep05:
        return "ConcreteFootstep05";
    case SfxId::ConcreteFootstep06:
        return "ConcreteFootstep06";
    case SfxId::ConcreteFootstep07:
        return "ConcreteFootstep07";
    case SfxId::ConcreteFootstep08:
        return "ConcreteFootstep08";
    case SfxId::ConcreteFootstep09:
        return "ConcreteFootstep09";
    case SfxId::ConcreteFootstep10:
        return "ConcreteFootstep10";
    case SfxId::ConcreteFootstep11:
        return "ConcreteFootstep11";
    case SfxId::ConcreteFootstep12:
        return "ConcreteFootstep12";
    case SfxId::ConcreteFootstep13:
        return "ConcreteFootstep13";
    case SfxId::ConcreteFootstep14:
        return "ConcreteFootstep14";
    case SfxId::ConcreteFootstep15:
        return "ConcreteFootstep15";
    case SfxId::ConcreteFootstep16:
        return "ConcreteFootstep16";
    case SfxId::ConcreteFootstep17:
        return "ConcreteFootstep17";
    case SfxId::DirtFootstep01:
        return "DirtFootstep01";
    case SfxId::DirtFootstep02:
        return "DirtFootstep02";
    case SfxId::DirtFootstep03:
        return "DirtFootstep03";
    case SfxId::DirtFootstep04:
        return "DirtFootstep04";
    case SfxId::DirtFootstep05:
        return "DirtFootstep05";
    case SfxId::DirtFootstep06:
        return "DirtFootstep06";
    case SfxId::DirtFootstep07:
        return "DirtFootstep07";
    case SfxId::DirtFootstep08:
        return "DirtFootstep08";
    case SfxId::DirtFootstep09:
        return "DirtFootstep09";
    case SfxId::DirtFootstep10:
        return "DirtFootstep10";
    case SfxId::DirtFootstep11:
        return "DirtFootstep11";
    case SfxId::DirtFootstep12:
        return "DirtFootstep12";
    case SfxId::DirtFootstep13:
        return "DirtFootstep13";
    case SfxId::DirtFootstep14:
        return "DirtFootstep14";
    case SfxId::Slide:
        return "Slide";
    case SfxId::DashSfx:
        return "DashSfx";
    case SfxId::GravityFlipSfx:
        return "GravityFlipSfx";
    case SfxId::GrappleSfx:
        return "GrappleSfx";
    case SfxId::RecallSfx:
        return "RecallSfx";
    case SfxId::GrenadeThrow:
        return "GrenadeThrow";
    case SfxId::VoiceStart:
        return "VoiceStart";
    case SfxId::VoiceStop:
        return "VoiceStop";
    case SfxId::MenuMusic:
        return "MenuMusic";
    case SfxId::GameMusic:
        return "GameMusic";
    case SfxId::UiHover01:
        return "UiHover01";
    case SfxId::UiHover02:
        return "UiHover02";
    case SfxId::UiConfirm01:
        return "UiConfirm01";
    case SfxId::UiConfirm02:
        return "UiConfirm02";
    case SfxId::UiBack01:
        return "UiBack01";
    case SfxId::UiToggle01:
        return "UiToggle01";
    case SfxId::UiSliderStep01:
        return "UiSliderStep01";
    case SfxId::UiModal01:
        return "UiModal01";
    case SfxId::UiSuccess01:
        return "UiSuccess01";
    case SfxId::UiError01:
        return "UiError01";
    case SfxId::UiError02:
        return "UiError02";
    case SfxId::UiDisabled01:
        return "UiDisabled01";
    case SfxId::UiDanger01:
        return "UiDanger01";
    case SfxId::GameMusicLoop:
        return "GameMusicLoop";
    default:
        return "Unknown";
    }
}

inline std::optional<SfxId> sfxIdFromName(std::string_view name) noexcept
{
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(SfxId::_Count); ++i) {
        const auto id = static_cast<SfxId>(i);
        if (name == sfxIdName(id))
            return id;
    }
    return std::nullopt;
}

inline const char* sfxCategoryName(SfxCategory category) noexcept
{
    switch (category) {
    case SfxCategory::Weapons:
        return "Weapons";
    case SfxCategory::Impacts:
        return "Impacts";
    case SfxCategory::Player:
        return "Player";
    case SfxCategory::Footsteps:
        return "Footsteps";
    case SfxCategory::Voice:
        return "Voice";
    case SfxCategory::Music:
        return "Music";
    case SfxCategory::UI:
        return "UI";
    default:
        return "Unknown";
    }
}
