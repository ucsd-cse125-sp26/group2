/// @file SfxSystem.cpp
/// @brief SDL-backed mixer, spatial SFX, and voice-stream playback facade.

#include "SfxSystem.hpp"

#include "ecs/components/FireField.hpp"
#include "ecs/components/GrenadeState.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/Raycast.hpp"
#include "ecs/physics/WorldData.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <string>

// minimp3 — header-only MP3 decoder (implementation defined exactly once here).
// Diagnostic suppression keeps compiler warnings from minimp3's C code out of
// our build output.
#ifdef _MSC_VER
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif

#define MINIMP3_IMPLEMENTATION
#include <minimp3_ex.h>

#ifdef _MSC_VER
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace
{

constexpr float kPi = 3.1415926535f;
constexpr float kDefaultVoiceChatGain = 2.0f;
constexpr float kMenuUiGain = 0.45f;

struct UiSoundActionConfig
{
    std::span<const SfxId> variants;
    float cooldownSeconds = 0.0f;
};

constexpr std::array<SfxId, 2> kUiHoverVariants{SfxId::UiHover01, SfxId::UiHover02};
constexpr std::array<SfxId, 2> kUiConfirmVariants{SfxId::UiConfirm01, SfxId::UiConfirm02};
constexpr std::array<SfxId, 1> kUiBackVariants{SfxId::UiBack01};
constexpr std::array<SfxId, 1> kUiToggleVariants{SfxId::UiToggle01};
constexpr std::array<SfxId, 1> kUiSliderVariants{SfxId::UiSliderStep01};
constexpr std::array<SfxId, 1> kUiModalVariants{SfxId::UiModal01};
constexpr std::array<SfxId, 1> kUiSuccessVariants{SfxId::UiSuccess01};
constexpr std::array<SfxId, 2> kUiErrorVariants{SfxId::UiError01, SfxId::UiError02};
constexpr std::array<SfxId, 1> kUiDisabledVariants{SfxId::UiDisabled01};

UiSoundActionConfig uiSoundActionConfig(UiSoundAction action) noexcept
{
    switch (action) {
    case UiSoundAction::Hover:
        return {kUiHoverVariants, 0.08f};
    case UiSoundAction::Confirm:
        return {kUiConfirmVariants, 0.03f};
    case UiSoundAction::Back:
        return {kUiBackVariants, 0.03f};
    case UiSoundAction::Toggle:
        return {kUiToggleVariants, 0.04f};
    case UiSoundAction::SliderStep:
        return {kUiSliderVariants, 0.08f};
    case UiSoundAction::ModalOpen:
    case UiSoundAction::ModalClose:
        return {kUiModalVariants, 0.10f};
    case UiSoundAction::Success:
        return {kUiSuccessVariants, 0.08f};
    case UiSoundAction::Error:
        return {kUiErrorVariants, 0.08f};
    case UiSoundAction::Disabled:
        return {kUiDisabledVariants, 0.08f};
    default:
        return {};
    }
}

bool sequenceNewer(std::uint16_t a, std::uint16_t b) noexcept
{
    return static_cast<std::int16_t>(a - b) > 0;
}

float softClip(float value) noexcept
{
    return std::clamp(value / (1.0f + std::fabs(value) * 0.35f), -1.0f, 1.0f);
}

std::string_view reloadEventForWeapon(WeaponType type) noexcept
{
    switch (type) {
    case WeaponType::Rifle:
        return "weapon.rifle.reload";
    case WeaponType::Rocket:
        return "weapon.rocket.reload";
    case WeaponType::RailGun:
        return "weapon.railgun.reload";
    case WeaponType::EnergyGun:
        return "weapon.energy.reload";
    case WeaponType::Shotgun:
        return "weapon.shotgun.reload";
    case WeaponType::HEGrenade:
    case WeaponType::Molotov:
    case WeaponType::Sticky:
    case WeaponType::None:
        return {};
    }
    return {};
}

std::string_view explosionEventForWeapon(WeaponType type) noexcept
{
    switch (type) {
    case WeaponType::Rocket:
        return "explosion.rocket";
    case WeaponType::Molotov:
        return "explosion.molotov";
    case WeaponType::HEGrenade:
    case WeaponType::Sticky:
        return "explosion.he";
    case WeaponType::Rifle:
    case WeaponType::RailGun:
    case WeaponType::EnergyGun:
    case WeaponType::Shotgun:
    case WeaponType::None:
        return "explosion";
    }
    return "explosion";
}

SDL_AudioDeviceID findAudioDeviceByName(std::string_view name, bool recording)
{
    if (name.empty())
        return recording ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;

    int count = 0;
    SDL_AudioDeviceID* devices = recording ? SDL_GetAudioRecordingDevices(&count) : SDL_GetAudioPlaybackDevices(&count);
    if (!devices)
        return recording ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;

    SDL_AudioDeviceID match = recording ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    for (int i = 0; i < count; ++i) {
        const char* deviceName = SDL_GetAudioDeviceName(devices[i]);
        if (deviceName && name == deviceName) {
            match = devices[i];
            break;
        }
    }
    SDL_free(devices);
    return match;
}

audio::AudioObjectId sfxAudioObjectForEntity(entt::entity entity) noexcept
{
    audio::StableId value = audio::stableHash("sfx.entity") ^ static_cast<audio::StableId>(entt::to_integral(entity));
    if (value == 0)
        value = 1;
    return audio::AudioObjectId{value};
}

} // namespace

bool SfxSystem::init()
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("[sfx] SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
        return false;
    }

    mixerSpec_.format = SDL_AUDIO_F32LE;
    mixerSpec_.channels = audio::k_mixerChannels;
    mixerSpec_.freq = audio::k_mixerSampleRate;

    if (!openDevice()) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    categoryVolumes_.fill(1.0f);
    cooldowns_.fill(0.0f);

    loadClip(SfxId::RifleFire, "Weapons/Rifle/Rifle_Shooting.wav", SfxCategory::Weapons, 0.8f, 0.10f);
    loadClip(SfxId::RocketFire, "Weapons/Rocket/Rocket_Shooting.wav", SfxCategory::Weapons, 0.7f, 0.80f);
    loadClip(SfxId::RailGunFire, "Weapons/Railgun/Railgun_Shooting.wav", SfxCategory::Weapons, 0.8f, 0.20f);
    loadClip(SfxId::EnergyGunFire, "Weapons/EnergyWeapon/Energy_Shooting_Start.wav", SfxCategory::Weapons, 0.7f, 0.0f);
    loadClip(SfxId::ShotgunFire, "Weapons/Shotgun/Shotgun_Shooting.wav", SfxCategory::Weapons, 0.85f, 0.20f);
    loadClip(SfxId::RifleReload, "Weapons/Rifle/Rifle_Reloading.wav", SfxCategory::Weapons, 0.78f, 0.0f);
    loadClip(SfxId::RocketReload, "Weapons/Rocket/Rocket_Reloading.wav", SfxCategory::Weapons, 0.8f, 0.0f);
    loadClip(SfxId::RailGunReload, "Weapons/Railgun/Railgun_Reloading.wav", SfxCategory::Weapons, 0.78f, 0.0f);
    loadClip(SfxId::EnergyReload, "Weapons/EnergyWeapon/Energy_Reloading.wav", SfxCategory::Weapons, 0.78f, 0.0f);
    loadClip(SfxId::ShotgunReload, "Weapons/Shotgun/Shotgun_Reloading.wav", SfxCategory::Weapons, 0.82f, 0.0f);
    loadClip(SfxId::RailGunCharge, "Weapons/Railgun/Railgun_Charge.wav", SfxCategory::Weapons, 0.9f, 0.0f);
    loadClip(SfxId::ChargeRifleLoad, "Weapons/Railgun/Railgun_Charge.wav", SfxCategory::Weapons, 0.9f, 0.0f);
    loadClip(SfxId::ChargeRifleShoot, "charge-rifle-shoot.wav", SfxCategory::Weapons, 1.0f, 0.20f);
    loadClip(SfxId::EnergyBeamLoop, "Weapons/EnergyWeapon/Energy_Shooting.wav", SfxCategory::Weapons, 0.6f, 0.0f);

    loadClip(SfxId::FleshHit, "Voicy_Flesh Bullet Impact SFX.mp3", SfxCategory::Impacts, 0.7f, 0.08f);
    loadClip(SfxId::Headshot, "Voicy_Headshot Rapid SFX.mp3", SfxCategory::Impacts, 0.8f, 0.08f);
    loadClip(SfxId::Explosion, "Voicy_Minecraft TNT Explosion.mp3", SfxCategory::Impacts, 1.0f, 0.30f);
    loadClip(SfxId::RocketExplosion, "Weapons/Rocket/Explosion_Rocket.wav", SfxCategory::Impacts, 1.0f, 0.20f);
    loadClip(SfxId::MolotovExplosion, "Weapons/Grenade/Explosion_Molotov.wav", SfxCategory::Impacts, 1.0f, 0.20f);
    loadClip(SfxId::HEExplosion, "Weapons/Grenade/Explosion_HE.wav", SfxCategory::Impacts, 1.0f, 0.20f);

    loadClip(SfxId::DamageTaken, "Voicy_roblox ooof.mp3", SfxCategory::Player, 0.8f, 0.30f);
    loadClip(SfxId::ArmorBreak, "Voicy_Fortnite Shield Break.mp3", SfxCategory::Player, 0.9f, 1.00f);
    loadClip(SfxId::Death, "Voicy_Minecraft Death Sound.mp3", SfxCategory::Player, 1.0f, 2.00f);
    loadClip(SfxId::Respawn, "Voicy_totem of undying sfx .mp3", SfxCategory::Player, 0.8f, 2.00f);
    loadClip(SfxId::KillConfirm, "Voicy_Pilot Killed Indicator SFX.mp3", SfxCategory::Player, 0.9f, 0.30f);
    loadClip(SfxId::Healing, "Voicy_Syringe SFX .mp3", SfxCategory::Player, 0.5f, 1.00f);
    loadClip(SfxId::ShieldRecharge, "Voicy_Halo Shield Recharge.mp3", SfxCategory::Player, 0.5f, 1.00f);

    synthesizeClip(SfxId::FootstepLight, SfxCategory::Footsteps, 0.40f, 0.06f);
    synthesizeClip(SfxId::FootstepHeavy, SfxCategory::Footsteps, 0.55f, 0.06f);
    loadClip(SfxId::ConcreteFootstep01, "Footsteps/concrete_ct_01.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep02, "Footsteps/concrete_ct_02.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep03, "Footsteps/concrete_ct_03.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep04, "Footsteps/concrete_ct_04.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep05, "Footsteps/concrete_ct_05.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep06, "Footsteps/concrete_ct_06.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep07, "Footsteps/concrete_ct_07.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep08, "Footsteps/concrete_ct_08.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep09, "Footsteps/concrete_ct_09.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep10, "Footsteps/concrete_ct_10.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep11, "Footsteps/concrete_ct_11.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep12, "Footsteps/concrete_ct_12.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep13, "Footsteps/concrete_ct_13.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep14, "Footsteps/concrete_ct_14.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep15, "Footsteps/concrete_ct_15.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep16, "Footsteps/concrete_ct_16.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::ConcreteFootstep17, "Footsteps/concrete_ct_17.wav", SfxCategory::Footsteps, 0.62f, 0.02f);
    loadClip(SfxId::DirtFootstep01, "Footsteps/dirt_01.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep02, "Footsteps/dirt_02.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep03, "Footsteps/dirt_03.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep04, "Footsteps/dirt_04.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep05, "Footsteps/dirt_05.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep06, "Footsteps/dirt_06.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep07, "Footsteps/dirt_07.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep08, "Footsteps/dirt_08.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep09, "Footsteps/dirt_09.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep10, "Footsteps/dirt_10.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep11, "Footsteps/dirt_11.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep12, "Footsteps/dirt_12.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep13, "Footsteps/dirt_13.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::DirtFootstep14, "Footsteps/dirt_14.wav", SfxCategory::Footsteps, 0.85f, 0.02f);
    loadClip(SfxId::Slide, "sliding.mp3", SfxCategory::Player, 0.75f, 0.0f);
    synthesizeClip(SfxId::DashSfx, SfxCategory::Player, 0.7f, 0.10f);
    synthesizeClip(SfxId::GravityFlipSfx, SfxCategory::Player, 0.7f, 0.10f);
    synthesizeClip(SfxId::GrappleSfx, SfxCategory::Player, 0.7f, 0.10f);
    synthesizeClip(SfxId::RecallSfx, SfxCategory::Player, 0.7f, 0.10f);
    loadClip(SfxId::GrenadeThrow, "Weapons/Grenade/Grenade_Throw.wav", SfxCategory::Weapons, 0.45f, 0.12f);
    synthesizeClip(SfxId::VoiceStart, SfxCategory::Voice, 0.20f, 0.05f);
    synthesizeClip(SfxId::VoiceStop, SfxCategory::Voice, 0.14f, 0.05f);
    loadClip(SfxId::MenuMusic, "Music/Gamesong1.wav", SfxCategory::Music, 0.8f, 0.0f);
    loadClip(SfxId::GameMusic, "Music/Gamesong2.wav", SfxCategory::Music, 0.8f, 0.0f);

    loadClip(
        SfxId::UiHover01, "MenuSFX/Bluezone_BC0268_switch_button_click_small_005.wav", SfxCategory::UI, 0.22f, 0.02f);
    loadClip(SfxId::UiHover02, "MenuSFX/Bleeps_Blops_Clicks_DDM29.wav", SfxCategory::UI, 0.18f, 0.02f);
    loadClip(SfxId::UiConfirm01, "MenuSFX/MenuSound_DDM23.1_Wav.wav", SfxCategory::UI, 0.32f, 0.02f);
    loadClip(SfxId::UiConfirm02,
             "MenuSFX/Bluezone_BC0268_switch_button_click_high_tech_interface_001.wav",
             SfxCategory::UI,
             0.26f,
             0.02f);
    loadClip(SfxId::UiBack01, "MenuSFX/Cancel Action_3.wav", SfxCategory::UI, 0.30f, 0.02f);
    loadClip(SfxId::UiToggle01,
             "MenuSFX/Bluezone_BC0268_switch_button_click_high_tech_mechanical_007_01.wav",
             SfxCategory::UI,
             0.24f,
             0.02f);
    loadClip(SfxId::UiSliderStep01, "MenuSFX/PM_FSSF2_USER_INTERFACE_SIMPLE_56.wav", SfxCategory::UI, 0.16f, 0.02f);
    loadClip(SfxId::UiModal01, "MenuSFX/PM_FSSF2_USER_INTERFACE_SIMPLE_56.wav", SfxCategory::UI, 0.24f, 0.02f);
    loadClip(SfxId::UiSuccess01,
             "MenuSFX/Bluezone_BC0268_switch_button_click_high_tech_interface_001.wav",
             SfxCategory::UI,
             0.34f,
             0.02f);
    loadClip(SfxId::UiError01, "MenuSFX/Access_Denied_High_DDM16.wav", SfxCategory::UI, 0.36f, 0.02f);
    loadClip(SfxId::UiError02, "MenuSFX/Deny Access Signal Lo-fi 12.wav", SfxCategory::UI, 0.36f, 0.02f);
    loadClip(SfxId::UiDisabled01, "MenuSFX/Deny Access Signal Lo-fi 12.wav", SfxCategory::UI, 0.24f, 0.02f);

    convertClipsToMixer();

    std::vector<std::string> manifestErrors;
    const char* base = SDL_GetBasePath();
    manifestPath_ = std::string(base ? base : "") + "assets/audio/audio_manifest.toml";
    if (!audioRuntime_.loadManifest(manifestPath_, &manifestErrors)) {
        audioRuntime_.loadDefaultManifest();
        SDL_Log("[sfx] Audio manifest unavailable at '%s'; using built-in graph", manifestPath_.c_str());
        for (const std::string& error : manifestErrors)
            SDL_Log("[sfx]   manifest: %s", error.c_str());
    } else {
        SDL_Log("[sfx] Loaded audio manifest '%s' (%zu clips, %zu nodes, %zu events)",
                manifestPath_.c_str(),
                audioRuntime_.manifest().clips().size(),
                audioRuntime_.manifest().nodes().size(),
                audioRuntime_.manifest().events().size());
    }

    warmUpDevice();

    int loaded = 0;
    for (const auto& clip : clips_) {
        if (clip.loaded)
            ++loaded;
    }

    SDL_Log("[sfx] SfxSystem initialized — %d/%d clips loaded, one mixer stream on device id=%u",
            loaded,
            static_cast<int>(SfxId::_Count),
            static_cast<unsigned>(device_));

    return true;
}

void SfxSystem::quit()
{
    if (!mixStream_)
        return;

    {
        std::lock_guard lock(mixerMutex_);
        for (Source& source : sources_)
            source = Source{};
        voiceSources_.clear();
        musicHandle_ = kInvalidSource;
        currentMusic_ = SfxId::_Count;
    }

    SDL_DestroyAudioStream(mixStream_);
    mixStream_ = nullptr;
    device_ = 0;
    physicalDeviceId_ = 0;

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_Log("[sfx] SfxSystem shut down");
}

void SfxSystem::play(SfxId id, float gain)
{
    play2D(id, gain);
}

SfxSystem::SourceHandle SfxSystem::play2D(SfxId id, float gain, float priority)
{
    return startSource(
        id, false, false, glm::vec3{0.0f}, glm::vec3{0.0f}, gain, priority, audio::kInvalidBus, 1.0f, 0, 0);
}

SfxSystem::SourceHandle SfxSystem::playUi(UiSoundAction action, float gain)
{
    const size_t actionIndex = static_cast<size_t>(action);
    if (actionIndex >= uiActionCooldowns_.size() || uiActionCooldowns_[actionIndex] > 0.0f)
        return kInvalidSource;

    const UiSoundActionConfig config = uiSoundActionConfig(action);
    std::array<SfxId, 4> loadedVariants{};
    std::size_t loadedCount = 0;
    for (const SfxId id : config.variants) {
        const size_t clipIndex = static_cast<size_t>(id);
        if (clipIndex < clips_.size() && clips_[clipIndex].loaded && !clips_[clipIndex].samples.empty() &&
            loadedCount < loadedVariants.size())
        {
            loadedVariants[loadedCount++] = id;
        }
    }
    if (loadedCount == 0)
        return kInvalidSource;

    const std::size_t selected = uiActionVariantCursors_[actionIndex] % loadedCount;
    ++uiActionVariantCursors_[actionIndex];
    uiActionCooldowns_[actionIndex] = config.cooldownSeconds;
    return play2D(loadedVariants[selected], gain * kMenuUiGain, 1.0f);
}

SfxSystem::SourceHandle
SfxSystem::play3D(SfxId id, const glm::vec3& position, const glm::vec3& velocity, float gain, float priority)
{
    return startSource(id, true, false, position, velocity, gain, priority, audio::kInvalidBus, 1.0f, 0, 0);
}

SfxSystem::SourceHandle
SfxSystem::startLoop(SfxId id, bool positional, const glm::vec3& position, float gain, float priority)
{
    return startSource(id, positional, true, position, glm::vec3{0.0f}, gain, priority, audio::kInvalidBus, 1.0f, 0, 0);
}

void SfxSystem::updateSource(SourceHandle handle, const glm::vec3& position, const glm::vec3& velocity, float gain)
{
    if (handle == kInvalidSource)
        return;
    std::lock_guard lock(mixerMutex_);
    Source* source = findSource(handle);
    if (!source)
        return;
    source->position = position;
    source->velocity = velocity;
    source->gain = gain;
    source->occluded = source->positional && isOccluded(position);
}

void SfxSystem::stopSource(SourceHandle handle)
{
    if (handle == kInvalidSource)
        return;
    std::lock_guard lock(mixerMutex_);
    Source* source = findSource(handle);
    if (!source)
        return;
    if (source->voiceStream)
        voiceSources_.erase(source->speaker.value);
    if (handle == musicHandle_) {
        musicHandle_ = kInvalidSource;
        currentMusic_ = SfxId::_Count;
    }
    *source = Source{};
}

void SfxSystem::playMusic(SfxId id, float gain)
{
    {
        std::lock_guard lock(mixerMutex_);
        if (currentMusic_ == id && findSource(musicHandle_))
            return;
    }

    stopMusic();
    musicHandle_ = startLoop(id, false, glm::vec3{0.0f}, gain, 2.0f);
    currentMusic_ = musicHandle_ == kInvalidSource ? SfxId::_Count : id;
}

void SfxSystem::stopMusic()
{
    if (musicHandle_ != kInvalidSource)
        stopSource(musicHandle_);
    musicHandle_ = kInvalidSource;
    currentMusic_ = SfxId::_Count;
}

void SfxSystem::setListener(const audio::ListenerState& listener)
{
    std::lock_guard lock(mixerMutex_);
    listener_ = listener;
    for (Source& source : sources_) {
        if (source.active && source.positional)
            source.occluded = isOccluded(source.position);
    }
}

void SfxSystem::setAudioObjectTransform(audio::AudioObjectId object,
                                        const glm::vec3& position,
                                        const glm::vec3& velocity)
{
    audioRuntime_.setObjectTransform(object, position, velocity);
}

void SfxSystem::removeAudioObject(audio::AudioObjectId object)
{
    audioRuntime_.removeObject(object);
}

void SfxSystem::setAudioRtpc(audio::AudioObjectId object, audio::RtpcId rtpc, float value)
{
    audioRuntime_.setRtpc(object, rtpc, value);
}

void SfxSystem::setAudioSwitch(audio::AudioObjectId object, audio::SwitchGroupId group, audio::SwitchValueId value)
{
    audioRuntime_.setSwitch(object, group, value);
}

void SfxSystem::setAudioState(audio::StateGroupId group, audio::StateValueId value)
{
    audioRuntime_.setState(group, value);
}

void SfxSystem::setAudioBusVolume(audio::AudioBusId bus, float volume)
{
    audioRuntime_.setBusVolume(bus, volume);
}

bool SfxSystem::reloadAudioManifest()
{
    std::vector<std::string> errors;
    if (manifestPath_.empty() || !audioRuntime_.loadManifest(manifestPath_, &errors)) {
        SDL_Log("[sfx] Audio manifest reload failed; keeping current graph");
        for (const std::string& error : errors)
            SDL_Log("[sfx]   manifest: %s", error.c_str());
        return false;
    }
    SDL_Log("[sfx] Reloaded audio manifest '%s'", manifestPath_.c_str());
    return true;
}

SfxSystem::SourceHandle SfxSystem::postAudioEvent(std::string_view eventName, audio::AudioObjectId object, float gain)
{
    SourceHandle first = kInvalidSource;
    for (const audio::AudioCommand& command : audioRuntime_.postEvent(eventName, object, gain)) {
        const SourceHandle handle = playCommand(command);
        if (first == kInvalidSource && handle != kInvalidSource)
            first = handle;
    }
    return first;
}

SfxSystem::SourceHandle
SfxSystem::postLocalAudioEvent(std::string_view eventName, audio::AudioObjectId object, float gain)
{
    SourceHandle first = kInvalidSource;
    for (audio::AudioCommand command : audioRuntime_.postEvent(eventName, object, gain)) {
        command.positional = false;
        const SourceHandle handle = playCommand(command);
        if (first == kInvalidSource && handle != kInvalidSource)
            first = handle;
    }
    return first;
}

void SfxSystem::submitVoiceFrame(ClientId speaker,
                                 std::uint16_t sequence,
                                 std::span<const float> monoPcm48k,
                                 const glm::vec3& position,
                                 const glm::vec3& velocity)
{
    if (!mixStream_ || monoPcm48k.empty() || monoPcm48k.size() > static_cast<std::size_t>(audio::k_mixerSampleRate))
        return;

    std::lock_guard lock(mixerMutex_);
    Source* source = findVoiceSource(speaker);
    if (!source) {
        source = acquireSource(3.0f, SfxId::_Count, audio::busId("VoiceChat"), 0, 12);
        if (!source)
            return;
        const SourceHandle handle = nextSourceHandle_++;
        if (handle == kInvalidSource)
            nextSourceHandle_ = 1;
        *source = Source{};
        source->active = true;
        source->voiceStream = true;
        source->positional = true;
        source->playingId = SfxId::_Count;
        source->handle = handle;
        source->speaker = speaker;
        source->gain = kDefaultVoiceChatGain;
        source->priority = 3.0f;
        source->bus = audio::busId("VoiceChat");
        source->busGain = audioRuntime_.busGain(source->bus);
        source->maxBusInstances = audioRuntime_.busMaxVoices(source->bus);
        voiceSources_[speaker.value] = handle;
    } else if (source->hasVoiceSeq && !sequenceNewer(sequence, source->newestVoiceSeq)) {
        return;
    }

    source->newestVoiceSeq = sequence;
    source->hasVoiceSeq = true;
    source->age = 0.0f;
    source->gain = kDefaultVoiceChatGain;
    source->position = position;
    source->velocity = velocity;
    source->occluded = isOccluded(position);
    source->voicePcm.insert(source->voicePcm.end(), monoPcm48k.begin(), monoPcm48k.end());
    if (source->voicePcm.size() > kMaxVoiceQueuedFrames) {
        const std::size_t trim = source->voicePcm.size() - kMaxVoiceQueuedFrames;
        source->voicePcm.erase(source->voicePcm.begin(), source->voicePcm.begin() + static_cast<std::ptrdiff_t>(trim));
        source->voiceReadFrame = source->voiceReadFrame > trim ? source->voiceReadFrame - trim : 0;
    }
}

void SfxSystem::stop(SfxId id)
{
    std::lock_guard lock(mixerMutex_);
    for (Source& source : sources_) {
        if (source.active && source.playingId == id) {
            if (source.voiceStream)
                voiceSources_.erase(source.speaker.value);
            source = Source{};
        }
    }
}

void SfxSystem::setCategoryVolume(SfxCategory cat, float v)
{
    categoryVolumes_[static_cast<size_t>(cat)] = std::clamp(v, 0.0f, 2.0f);
}

void SfxSystem::setPlaybackDeviceName(std::string_view name)
{
    if (playbackDeviceName_ == name)
        return;

    playbackDeviceName_ = std::string(name);
    if (mixStream_)
        pendingReopen_ = true;
}

float SfxSystem::categoryVolume(SfxCategory cat) const
{
    return categoryVolumes_[static_cast<size_t>(cat)];
}

void SfxSystem::onWeaponFired(const WeaponFiredEvent& e)
{
    const audio::AudioObjectId object = audio::objectId("event.weapon_fire");
    setAudioObjectTransform(object, e.origin);
    const auto postFireSound = [&](std::string_view eventName) {
        if (e.localPlayer)
            postLocalAudioEvent(eventName, object);
        else
            postAudioEvent(eventName, object);
    };

    switch (e.type) {
    case WeaponType::Rifle:
        postFireSound("weapon.rifle.fire");
        break;
    case WeaponType::Rocket:
        postFireSound("weapon.rocket.fire");
        break;
    case WeaponType::RailGun:
        postFireSound("weapon.railgun.fire");
        break;
    case WeaponType::EnergyGun:
        postFireSound("weapon.energy.start");
        break;
    case WeaponType::Shotgun:
        postFireSound("weapon.shotgun.fire");
        break;
    case WeaponType::HEGrenade:
    case WeaponType::Molotov:
    case WeaponType::Sticky:
    case WeaponType::None:
        break;
    }
}

void SfxSystem::onExplosion(const ExplosionEvent& e)
{
    const audio::AudioObjectId object = audio::objectId("event.explosion");
    setAudioObjectTransform(object, e.pos);
    postAudioEvent(explosionEventForWeapon(e.weaponType), object, std::clamp(e.blastRadius / 220.0f, 0.75f, 1.8f));
}

void SfxSystem::handleEvent(const SDL_Event& event)
{
#ifdef __APPLE__
    if (event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED && !event.adevice.recording) {
        SDL_Log("[sfx] Playback device removed (id=%u), scheduling reopen", static_cast<unsigned>(event.adevice.which));
        pendingReopen_ = true;
    } else if (event.type == SDL_EVENT_AUDIO_DEVICE_ADDED && !event.adevice.recording) {
        SDL_Log("[sfx] New playback device available (id=%u), scheduling reopen",
                static_cast<unsigned>(event.adevice.which));
        pendingReopen_ = true;
    }
#else
    (void)event;
#endif
}

void SfxSystem::update(float dt)
{
    if (pendingReopen_) {
        pendingReopen_ = false;
        reopenDevice();
    }

    if (!mixStream_)
        return;

    for (auto& cd : cooldowns_) {
        if (cd > 0.0f)
            cd = (cd > dt) ? (cd - dt) : 0.0f;
    }
    for (auto& cd : uiActionCooldowns_) {
        if (cd > 0.0f)
            cd = (cd > dt) ? (cd - dt) : 0.0f;
    }
    if (healingSoundCooldown_ > 0.0f)
        healingSoundCooldown_ = (healingSoundCooldown_ > dt) ? (healingSoundCooldown_ - dt) : 0.0f;

    {
        std::lock_guard lock(mixerMutex_);
        for (Source& source : sources_) {
            if (!source.active)
                continue;
            source.age += dt;
            if (source.voiceStream && source.voicePcm.empty() && source.age > 0.35f) {
                voiceSources_.erase(source.speaker.value);
                source = Source{};
                continue;
            }
            source.busGain = audioRuntime_.busGain(source.bus);
        }
    }
}

void SfxSystem::update(float dt, const Registry& registry)
{
    update(dt);

    if (!mixStream_)
        return;

    for (const auto entity : registry.view<const LocalPlayer, const Health, const PlayerMatchStats>()) {
        const auto& h = registry.get<const Health>(entity);
        const auto& stats = registry.get<const PlayerMatchStats>(entity);

        if (!stateInitialized_) {
            prevHealth_ = h.health;
            prevArmor_ = h.armor;
            prevDeaths_ = stats.deaths;
            prevKills_ = stats.kills;
            stateInitialized_ = true;
            break;
        }

        const bool justDied = stats.deaths > prevDeaths_;
        if (justDied) {
            postAudioEvent("player.death");
        } else {
            const bool healthLost = h.health < prevHealth_ || h.armor < prevArmor_;
            const bool armorJustBroke = prevArmor_ > 0.0f && h.armor <= 0.0f;
            if (healthLost)
                postAudioEvent("player.damage");
            if (armorJustBroke)
                postAudioEvent("player.armor_break");
            const bool healing = h.health > prevHealth_ || h.armor > prevArmor_;
            if (healing && healingSoundCooldown_ <= 0.0f) {
                postAudioEvent("player.healing");
                healingSoundCooldown_ = 1.0f;
            }
        }

        if (stats.kills > prevKills_)
            postAudioEvent("player.kill_confirm");

        prevHealth_ = h.health;
        prevArmor_ = h.armor;
        prevDeaths_ = stats.deaths;
        prevKills_ = stats.kills;
        break;
    }

    for (const auto entity : registry.view<const LocalPlayer, const WeaponState>()) {
        const auto& weapon = registry.get<const WeaponState>(entity);
        const GunInstance& gun = getEquippedGun(weapon);

        const bool justStartedReload = gun.isReloading && !prevLocalReloading_;
        if (justStartedReload) {
            const std::string_view eventName = reloadEventForWeapon(gun.type);
            if (!eventName.empty())
                postLocalAudioEvent(eventName, sfxAudioObjectForEntity(entity));
        }

        const bool railgunCharging = gun.type == WeaponType::RailGun && gun.chargeTime > 0.0f && !gun.isReloading;
        if (railgunCharging && !prevLocalRailgunCharging_)
            postLocalAudioEvent("weapon.railgun.charge_start", sfxAudioObjectForEntity(entity));

        prevLocalReloading_ = gun.isReloading;
        prevLocalRailgunCharging_ = railgunCharging;
        break;
    }

    std::vector<entt::entity> liveGrenadeOwners;
    liveGrenadeOwners.reserve(prevGrenadeCooldowns_.size());
    registry.view<const GrenadeState>().each([&](entt::entity entity, const GrenadeState& grenades) {
        liveGrenadeOwners.push_back(entity);
        const float prevCooldown = prevGrenadeCooldowns_[entity];
        if (grenades.cooldown > 0.0f && prevCooldown <= 0.0f) {
            const bool isLocal = registry.all_of<LocalPlayer>(entity);
            const audio::AudioObjectId object = sfxAudioObjectForEntity(entity);
            if (const auto* pos = registry.try_get<Position>(entity))
                setAudioObjectTransform(object, pos->value);

            if (isLocal)
                postLocalAudioEvent("weapon.grenade.throw", object);
            else
                postAudioEvent("weapon.grenade.throw", object);
        }
        prevGrenadeCooldowns_[entity] = grenades.cooldown;
    });
    for (auto it = prevGrenadeCooldowns_.begin(); it != prevGrenadeCooldowns_.end();) {
        if (std::find(liveGrenadeOwners.begin(), liveGrenadeOwners.end(), it->first) == liveGrenadeOwners.end())
            it = prevGrenadeCooldowns_.erase(it);
        else
            ++it;
    }

    std::vector<entt::entity> liveFireFields;
    liveFireFields.reserve(knownFireFields_.size());
    registry.view<const FireField>().each([&](entt::entity entity, const FireField& field) {
        liveFireFields.push_back(entity);
        if (!knownFireFields_.contains(entity)) {
            const audio::AudioObjectId object = sfxAudioObjectForEntity(entity);
            setAudioObjectTransform(object, field.position);
            postAudioEvent("explosion.molotov", object, 1.0f);
            knownFireFields_[entity] = true;
        }
    });
    for (auto it = knownFireFields_.begin(); it != knownFireFields_.end();) {
        if (std::find(liveFireFields.begin(), liveFireFields.end(), it->first) == liveFireFields.end())
            it = knownFireFields_.erase(it);
        else
            ++it;
    }
}

bool SfxSystem::loadClip(SfxId id, const char* filename, SfxCategory cat, float gain, float cooldownSecs)
{
    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "assets/sounds/" + filename;
    SoundClip& clip = clips_[static_cast<size_t>(id)];

    const std::string fname = filename;
    const bool isWav = fname.ends_with(".wav") || fname.ends_with(".WAV");
    if (isWav) {
        SDL_AudioSpec wavSpec{};
        Uint8* wavBuf = nullptr;
        Uint32 wavLen = 0;
        if (!SDL_LoadWAV(path.c_str(), &wavSpec, &wavBuf, &wavLen) || !wavBuf) {
            SDL_Log("[sfx] SDL_LoadWAV failed for '%s': %s", filename, SDL_GetError());
            if (wavBuf)
                SDL_free(wavBuf);
            return false;
        }

        clip.pcmData.assign(wavBuf, wavBuf + wavLen);
        SDL_free(wavBuf);
        clip.spec = wavSpec;
        const int bitsPerSample = SDL_AUDIO_BITSIZE(wavSpec.format);
        const int bytesPerFrame = (bitsPerSample / 8) * wavSpec.channels;
        clip.durationSeconds =
            bytesPerFrame > 0 && wavSpec.freq > 0
                ? static_cast<float>(wavLen / static_cast<Uint32>(bytesPerFrame)) / static_cast<float>(wavSpec.freq)
                : 1.0f;

        SDL_Log("[sfx]   %-45s  %.2fs  %5dHz  %dch  %5u KB  [wav]",
                filename,
                static_cast<double>(clip.durationSeconds),
                wavSpec.freq,
                wavSpec.channels,
                wavLen / 1024u);
    } else {
        mp3dec_t dec{};
        mp3dec_file_info_t info{};
        const int rc = mp3dec_load(&dec, path.c_str(), &info, nullptr, nullptr);
        if (rc != 0 || info.samples == 0) {
            SDL_Log("[sfx] Failed to load '%s' (rc=%d, path=%s)", filename, rc, path.c_str());
            if (info.buffer)
                free(info.buffer);
            return false;
        }

        const size_t byteCount = info.samples * sizeof(mp3d_sample_t);
        clip.pcmData.assign(reinterpret_cast<const uint8_t*>(info.buffer),
                            reinterpret_cast<const uint8_t*>(info.buffer) + byteCount);
        free(info.buffer);
        clip.spec.format = SDL_AUDIO_S16LE;
        clip.spec.channels = info.channels;
        clip.spec.freq = info.hz;
        clip.durationSeconds =
            info.channels > 0 && info.hz > 0
                ? static_cast<float>(info.samples / static_cast<size_t>(info.channels)) / static_cast<float>(info.hz)
                : 1.0f;

        SDL_Log("[sfx]   %-45s  %.2fs  %5dHz  %dch  %5zu KB  [mp3]",
                filename,
                static_cast<double>(clip.durationSeconds),
                info.hz,
                info.channels,
                byteCount / 1024);
    }

    clip.category = cat;
    clip.defaultGain = gain;
    clip.minCooldown = cooldownSecs;
    clip.loaded = true;
    return true;
}

void SfxSystem::synthesizeClip(SfxId id, SfxCategory cat, float gain, float cooldownSecs)
{
    SoundClip& clip = clips_[static_cast<size_t>(id)];

    int frames = 2800;
    switch (id) {
    case SfxId::FootstepHeavy:
        frames = 4200;
        break;
    case SfxId::DashSfx:
    case SfxId::GrappleSfx:
        frames = 8000;
        break;
    case SfxId::GravityFlipSfx:
    case SfxId::RecallSfx:
        frames = 12000;
        break;
    default:
        break;
    }

    clip.samples.assign(static_cast<std::size_t>(frames) * 2u, 0.0f);
    std::uint32_t seed = 0x1234567u + static_cast<std::uint32_t>(id) * 101u;
    const float sampleRate = static_cast<float>(audio::k_mixerSampleRate);

    for (int i = 0; i < frames; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float noise = (static_cast<float>((seed >> 9u) & 0xffffu) / 32767.5f) - 1.0f;
        const float t = static_cast<float>(i) / static_cast<float>(frames);
        const float secs = static_cast<float>(i) / sampleRate;
        float sample = 0.0f;

        switch (id) {
        case SfxId::DashSfx: {
            // Rising whoosh: pitch sweeps up, brightens, snappy decay.
            const float pitch = 220.0f + 900.0f * t;
            const float env = std::exp(-t * 2.5f) * (1.0f - std::exp(-t * 18.0f));
            const float body = std::sin(2.0f * kPi * pitch * secs);
            const float harm = std::sin(2.0f * kPi * pitch * 2.0f * secs) * 0.4f;
            sample = (body * 0.5f + harm * 0.3f + noise * 0.25f) * env * 0.45f;
            break;
        }
        case SfxId::GravityFlipSfx: {
            // Eerie warble: low sine waver + bell-like upper octave.
            const float baseHz = 110.0f;
            const float wobble = std::sin(2.0f * kPi * 6.0f * secs) * 35.0f;
            const float pitch = baseHz + wobble;
            const float env = std::sin(kPi * std::min(t, 1.0f)) * std::exp(-t * 1.2f);
            const float low = std::sin(2.0f * kPi * pitch * secs);
            const float bell = std::sin(2.0f * kPi * (pitch * 4.03f) * secs) * 0.35f;
            sample = (low * 0.55f + bell * 0.35f + noise * 0.05f) * env * 0.5f;
            break;
        }
        case SfxId::GrappleSfx: {
            // Metallic twang: descending pitch, sharp attack.
            const float pitch = 740.0f - 380.0f * t;
            const float env = std::exp(-t * 4.0f) * (1.0f - std::exp(-t * 60.0f));
            const float body = std::sin(2.0f * kPi * pitch * secs);
            const float ring = std::sin(2.0f * kPi * pitch * 1.5f * secs) * 0.35f;
            sample = (body * 0.55f + ring * 0.25f + noise * 0.15f) * env * 0.55f;
            break;
        }
        case SfxId::RecallSfx: {
            // Rewind: pitch slides up over a long tail with shimmer.
            const float pitch = 200.0f + 380.0f * t * t;
            const float env = std::sin(kPi * std::min(t, 1.0f)) * 0.9f;
            const float body = std::sin(2.0f * kPi * pitch * secs);
            const float shimmer = std::sin(2.0f * kPi * pitch * 2.99f * secs) * 0.4f;
            const float trem = 0.5f + 0.5f * std::sin(2.0f * kPi * 14.0f * secs);
            sample = (body * 0.45f + shimmer * 0.35f) * env * trem * 0.55f;
            break;
        }
        case SfxId::GrenadeThrow: {
            const float env = std::exp(-t * 12.0f);
            const float tone = std::sin(2.0f * kPi * 220.0f * secs);
            sample = (noise * 0.55f + tone * 0.2f) * env * 0.32f;
            break;
        }
        default: {
            const float env = std::exp(-t * (id == SfxId::FootstepHeavy ? 8.0f : 12.0f));
            const float tone = std::sin(2.0f * kPi * 95.0f * secs);
            sample = (noise * 0.55f + tone * 0.2f) * env * 0.32f;
            break;
        }
        }

        clip.samples[static_cast<std::size_t>(i) * 2u] = sample;
        clip.samples[static_cast<std::size_t>(i) * 2u + 1u] = sample;
    }
    clip.spec = mixerSpec_;
    clip.frameCount = static_cast<std::size_t>(frames);
    clip.durationSeconds = static_cast<float>(frames) / sampleRate;
    clip.category = cat;
    clip.defaultGain = gain;
    clip.minCooldown = cooldownSecs;
    clip.loaded = true;
}

void SfxSystem::convertClipToMixer(SoundClip& clip, const char* debugName)
{
    if (!clip.loaded || clip.pcmData.empty() || !clip.samples.empty())
        return;

    Uint8* dstBuf = nullptr;
    int dstLen = 0;
    if (!SDL_ConvertAudioSamples(
            &clip.spec, clip.pcmData.data(), static_cast<int>(clip.pcmData.size()), &mixerSpec_, &dstBuf, &dstLen))
    {
        SDL_Log("[sfx] mixer convert failed for %s: %s", debugName, SDL_GetError());
        return;
    }

    const auto* floats = reinterpret_cast<const float*>(dstBuf);
    const std::size_t floatCount = static_cast<std::size_t>(dstLen) / sizeof(float);
    clip.samples.assign(floats, floats + floatCount);
    SDL_free(dstBuf);
    clip.pcmData.clear();
    clip.spec = mixerSpec_;
    clip.frameCount = clip.samples.size() / static_cast<std::size_t>(audio::k_mixerChannels);
    clip.durationSeconds = static_cast<float>(clip.frameCount) / static_cast<float>(audio::k_mixerSampleRate);
}

void SfxSystem::convertClipsToMixer()
{
    int converted = 0;
    for (size_t i = 0; i < clips_.size(); ++i) {
        const std::size_t before = clips_[i].samples.size();
        convertClipToMixer(clips_[i], sfxIdName(static_cast<SfxId>(i)));
        if (clips_[i].samples.size() != before)
            ++converted;
    }
    SDL_Log("[sfx] Converted %d clips to mixer format (%d Hz, stereo F32)", converted, mixerSpec_.freq);
}

SfxSystem::Source* SfxSystem::acquireSource(
    float priority, SfxId id, audio::AudioBusId bus, std::uint16_t maxInstances, std::uint16_t maxBusInstances)
{
    auto stealBest = [&](auto predicate) -> Source* {
        Source* best = nullptr;
        float bestScore = std::numeric_limits<float>::max();
        for (Source& source : sources_) {
            if (!source.active || !predicate(source))
                continue;
            const float score = source.priority - source.age * 0.02f;
            if (score < bestScore) {
                bestScore = score;
                best = &source;
            }
        }
        if (!best || best->priority > priority + 0.25f)
            return nullptr;
        if (best->voiceStream)
            voiceSources_.erase(best->speaker.value);
        ++sfxStats_.stolenSources;
        *best = Source{};
        return best;
    };

    if (maxInstances > 0 && id != SfxId::_Count) {
        std::uint16_t count = 0;
        for (const Source& source : sources_) {
            if (source.active && source.playingId == id)
                ++count;
        }
        if (count >= maxInstances) {
            if (Source* stolen = stealBest([&](const Source& source) { return source.playingId == id; }))
                return stolen;
            return nullptr;
        }
    }

    if (maxBusInstances > 0 && bus.value != 0) {
        std::uint16_t count = 0;
        for (const Source& source : sources_) {
            if (source.active && source.bus == bus)
                ++count;
        }
        if (count >= maxBusInstances) {
            if (Source* stolen = stealBest([&](const Source& source) { return source.bus == bus; }))
                return stolen;
            return nullptr;
        }
    }

    for (Source& source : sources_) {
        if (!source.active)
            return &source;
    }

    Source* best = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (Source& source : sources_) {
        const float score = source.priority - source.age * 0.02f;
        if (score < bestScore) {
            bestScore = score;
            best = &source;
        }
    }
    if (!best || best->priority > priority + 4.0f)
        return nullptr;
    if (best->voiceStream)
        voiceSources_.erase(best->speaker.value);
    ++sfxStats_.stolenSources;
    *best = Source{};
    return best;
}

SfxSystem::Source* SfxSystem::findSource(SourceHandle handle)
{
    if (handle == kInvalidSource)
        return nullptr;
    for (Source& source : sources_) {
        if (source.active && source.handle == handle)
            return &source;
    }
    return nullptr;
}

SfxSystem::Source* SfxSystem::findVoiceSource(ClientId speaker)
{
    const auto it = voiceSources_.find(speaker.value);
    if (it == voiceSources_.end())
        return nullptr;
    Source* source = findSource(it->second);
    if (!source || !source->voiceStream) {
        voiceSources_.erase(it);
        return nullptr;
    }
    return source;
}

SfxSystem::SourceHandle SfxSystem::startSource(SfxId id,
                                               bool positional,
                                               bool loop,
                                               const glm::vec3& position,
                                               const glm::vec3& velocity,
                                               float gain,
                                               float priority,
                                               audio::AudioBusId bus,
                                               float busGain,
                                               std::uint16_t maxInstances,
                                               std::uint16_t maxBusInstances,
                                               float cooldownOverrideSeconds,
                                               float fullGainDistance,
                                               float silentDistance)
{
    if (!mixStream_)
        return kInvalidSource;

    const size_t idx = static_cast<size_t>(id);
    if (idx >= clips_.size())
        return kInvalidSource;
    const SoundClip& clip = clips_[idx];
    if (!clip.loaded || clip.samples.empty())
        return kInvalidSource;
    if (cooldowns_[idx] > 0.0f) {
        ++sfxStats_.droppedByCooldown;
        return kInvalidSource;
    }
    cooldowns_[idx] = cooldownOverrideSeconds >= 0.0f ? cooldownOverrideSeconds : clip.minCooldown;

    std::lock_guard lock(mixerMutex_);
    Source* source = acquireSource(priority, id, bus, maxInstances, maxBusInstances);
    if (!source) {
        ++sfxStats_.droppedByLimit;
        return kInvalidSource;
    }

    const SourceHandle handle = nextSourceHandle_++;
    if (handle == kInvalidSource)
        nextSourceHandle_ = 1;
    *source = Source{};
    source->active = true;
    source->loop = loop;
    source->positional = positional;
    source->playingId = id;
    source->handle = handle;
    source->gain = gain;
    source->priority = priority;
    source->bus = bus;
    source->busGain = busGain;
    source->maxInstances = maxInstances;
    source->maxBusInstances = maxBusInstances;
    source->fullGainDistance = fullGainDistance;
    source->silentDistance = silentDistance;
    source->position = position;
    source->velocity = velocity;
    source->occluded = positional && isOccluded(position);
    ++sfxStats_.sourcesStarted;
    return source->handle;
}

SfxSystem::SourceHandle SfxSystem::playCommand(const audio::AudioCommand& command)
{
    if (command.type == audio::AudioCommandType::StopClip) {
        stop(command.sfx);
        return kInvalidSource;
    }
    return startSource(command.sfx,
                       command.positional,
                       command.loop,
                       command.position,
                       command.velocity,
                       command.gain,
                       command.priority,
                       command.bus,
                       audioRuntime_.busGain(command.bus),
                       command.maxInstances,
                       command.maxBusInstances,
                       command.cooldownSeconds,
                       command.fullGainDistance,
                       command.silentDistance);
}

float SfxSystem::effectiveGain(SfxId id, float extraGain) const
{
    const SoundClip& clip = clips_[static_cast<size_t>(id)];
    return masterVolume_ * categoryVolumes_[static_cast<size_t>(clip.category)] * clip.defaultGain * extraGain;
}

bool SfxSystem::isOccluded(const glm::vec3& position) const
{
    const glm::vec3 delta = position - listener_.position;
    const float distance = glm::length(delta);
    if (distance <= 1.0f)
        return false;
    const physics::HitscanHit hit = physics::raycastWorld(listener_.position, delta / distance, physics::activeWorld());
    return hit.hit && hit.distance > 1.0f && hit.distance < distance - 24.0f;
}

bool SfxSystem::openDevice()
{
    if (mixStream_)
        return true;

#ifdef __APPLE__
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");
#endif

    const SDL_AudioDeviceID requestedDevice = findAudioDeviceByName(playbackDeviceName_, false);
    if (!playbackDeviceName_.empty() && requestedDevice == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) {
        SDL_Log("[sfx] Playback device '%s' unavailable; using system default", playbackDeviceName_.c_str());
    }
    mixStream_ = SDL_OpenAudioDeviceStream(requestedDevice, &mixerSpec_, &SfxSystem::mixCallback, this);

    if (!mixStream_) {
        SDL_Log("[sfx] SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        return false;
    }
    device_ = SDL_GetAudioStreamDevice(mixStream_);
    physicalDeviceId_ = device_;
    SDL_ResumeAudioStreamDevice(mixStream_);
    return true;
}

void SfxSystem::reopenDevice()
{
    SDL_Log("[sfx] Reopening audio mixer stream...");
    {
        std::lock_guard lock(mixerMutex_);
        for (Source& source : sources_)
            source = Source{};
        voiceSources_.clear();
    }
    if (mixStream_) {
        SDL_DestroyAudioStream(mixStream_);
        mixStream_ = nullptr;
    }
    device_ = 0;
    physicalDeviceId_ = 0;

    if (openDevice()) {
        warmUpDevice();
        SDL_Log("[sfx] Reopened audio on device %u", static_cast<unsigned>(device_));
    } else {
        SDL_Log("[sfx] No audio device available after reopen — running mute");
    }
    stateInitialized_ = false;
}

void SfxSystem::warmUpDevice()
{
    if (!mixStream_)
        return;
    constexpr int kWarmupFrames = 1024;
    std::array<float, static_cast<std::size_t>(kWarmupFrames * audio::k_mixerChannels)> silence{};
    SDL_PutAudioStreamData(mixStream_, silence.data(), static_cast<int>(silence.size() * sizeof(float)));
    SDL_Delay(20);
}

void SfxSystem::mixCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount)
{
    (void)totalAmount;
    static_cast<SfxSystem*>(userdata)->mixIntoStream(stream, additionalAmount);
}

void SfxSystem::mixIntoStream(SDL_AudioStream* stream, int additionalAmount)
{
    if (additionalAmount <= 0)
        return;

    const int bytesPerFrame = static_cast<int>(sizeof(float) * audio::k_mixerChannels);
    const int frames = std::clamp((additionalAmount + bytesPerFrame - 1) / bytesPerFrame, 128, 4096);
    thread_local std::vector<float> mix;
    thread_local std::vector<float> reverbSend;
    mix.assign(static_cast<std::size_t>(frames * audio::k_mixerChannels), 0.0f);
    reverbSend.assign(static_cast<std::size_t>(frames), 0.0f);

    {
        std::lock_guard lock(mixerMutex_);
        for (Source& source : sources_) {
            if (!source.active)
                continue;

            audio::SpatialParams spatial;
            if (source.positional)
                spatial = audio::evaluateSpatial(source.position,
                                                 source.velocity,
                                                 listener_,
                                                 source.occluded,
                                                 source.fullGainDistance,
                                                 source.silentDistance);
            if (!spatial.audible) {
                if (source.loop && !source.voiceStream && source.playingId != SfxId::_Count) {
                    const SoundClip& clip = clips_[static_cast<size_t>(source.playingId)];
                    if (clip.frameCount > 0)
                        source.cursor =
                            std::fmod(source.cursor + static_cast<float>(frames), static_cast<float>(clip.frameCount));
                    sfxStats_.virtualizedFrames += static_cast<std::uint64_t>(frames);
                }
                continue;
            }

            const float baseGain =
                source.voiceStream
                    ? masterVolume_ * categoryVolumes_[static_cast<size_t>(SfxCategory::Voice)] * source.gain
                    : effectiveGain(source.playingId, source.gain) * source.busGain;
            const float gain = baseGain * (source.positional ? spatial.gain : 1.0f);
            if (gain <= 0.0001f)
                continue;

            for (int frame = 0; frame < frames; ++frame) {
                float dryL = 0.0f;
                float dryR = 0.0f;
                if (source.voiceStream) {
                    if (source.voiceReadFrame >= source.voicePcm.size()) {
                        source.voicePcm.clear();
                        source.voiceReadFrame = 0;
                        break;
                    }
                    const float mono = source.voicePcm[source.voiceReadFrame++];
                    dryL = mono;
                    dryR = mono;
                    if (source.voiceReadFrame >= source.voicePcm.size()) {
                        source.voicePcm.clear();
                        source.voiceReadFrame = 0;
                    }
                } else {
                    const SoundClip& clip = clips_[static_cast<size_t>(source.playingId)];
                    if (clip.frameCount == 0)
                        break;
                    const std::size_t i0 = static_cast<std::size_t>(source.cursor);
                    if (i0 >= clip.frameCount) {
                        if (source.loop) {
                            source.cursor = std::fmod(source.cursor, static_cast<float>(clip.frameCount));
                        } else {
                            source = Source{};
                            break;
                        }
                    }
                    const std::size_t base = static_cast<std::size_t>(source.cursor) * 2u;
                    const std::size_t nextBase =
                        (std::min(static_cast<std::size_t>(source.cursor) + 1u, clip.frameCount - 1u)) * 2u;
                    const float frac = source.cursor - std::floor(source.cursor);
                    dryL = clip.samples[base] + (clip.samples[nextBase] - clip.samples[base]) * frac;
                    dryR = clip.samples[base + 1u] + (clip.samples[nextBase + 1u] - clip.samples[base + 1u]) * frac;
                    source.cursor += std::clamp(spatial.dopplerRatio, 0.5f, 1.75f);
                }

                if (source.positional) {
                    const float mono = (dryL + dryR) * 0.5f;
                    dryL = mono * spatial.left;
                    dryR = mono * spatial.right;
                }
                if (spatial.lowPass < 0.99f) {
                    source.lowPassStateL += spatial.lowPass * (dryL - source.lowPassStateL);
                    source.lowPassStateR += spatial.lowPass * (dryR - source.lowPassStateR);
                    dryL = source.lowPassStateL;
                    dryR = source.lowPassStateR;
                }

                const std::size_t out = static_cast<std::size_t>(frame) * 2u;
                mix[out] += dryL * gain;
                mix[out + 1u] += dryR * gain;
                reverbSend[static_cast<std::size_t>(frame)] += (dryL + dryR) * 0.5f * gain * spatial.reverbSend;
            }
        }

        constexpr std::size_t kDelaySize = 48000;
        constexpr std::size_t kTapA = 2112;
        constexpr std::size_t kTapB = 3408;
        for (int frame = 0; frame < frames; ++frame) {
            const std::size_t readA = (reverbWrite_ + kDelaySize - kTapA) % kDelaySize;
            const std::size_t readB = (reverbWrite_ + kDelaySize - kTapB) % kDelaySize;
            const float wetL = reverbDelayL_[readA] * 0.34f + reverbDelayR_[readB] * 0.16f;
            const float wetR = reverbDelayR_[readA] * 0.34f + reverbDelayL_[readB] * 0.16f;
            const std::size_t out = static_cast<std::size_t>(frame) * 2u;
            mix[out] = softClip(mix[out] + wetL);
            mix[out + 1u] = softClip(mix[out + 1u] + wetR);
            const float send = reverbSend[static_cast<std::size_t>(frame)];
            reverbDelayL_[reverbWrite_] = send + wetL * 0.22f;
            reverbDelayR_[reverbWrite_] = send + wetR * 0.22f;
            reverbWrite_ = (reverbWrite_ + 1u) % kDelaySize;
        }
    }

    SDL_PutAudioStreamData(stream, mix.data(), static_cast<int>(mix.size() * sizeof(float)));
}

std::uint32_t SfxSystem::activeSourceCount() const noexcept
{
    std::uint32_t count = 0;
    for (const Source& source : sources_) {
        if (source.active)
            ++count;
    }
    return count;
}

std::uint32_t SfxSystem::activeVoiceSourceCount() const noexcept
{
    std::uint32_t count = 0;
    for (const Source& source : sources_) {
        if (source.active && source.voiceStream)
            ++count;
    }
    return count;
}

float SfxSystem::clipDuration(SfxId id) const noexcept
{
    const std::size_t idx = static_cast<std::size_t>(id);
    if (idx >= clips_.size())
        return 0.0f;
    const SoundClip& clip = clips_[idx];
    return clip.loaded ? clip.durationSeconds : 0.0f;
}
