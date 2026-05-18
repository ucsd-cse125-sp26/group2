/// @file SfxSystem.cpp
/// @brief SDL-backed mixer, spatial SFX, and voice-stream playback facade.

#include "SfxSystem.hpp"

#include "ecs/components/Health.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/physics/Raycast.hpp"
#include "ecs/physics/WorldData.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
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

const char* sfxIdName(SfxId id)
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
    case SfxId::FleshHit:
        return "FleshHit";
    case SfxId::Headshot:
        return "Headshot";
    case SfxId::Explosion:
        return "Explosion";
    case SfxId::DamageTaken:
        return "DamageTaken";
    case SfxId::ArmorBreak:
        return "ArmorBreak";
    case SfxId::Death:
        return "Death";
    case SfxId::Respawn:
        return "Respawn";
    case SfxId::KillConfirm:
        return "KillConfirm";
    case SfxId::Healing:
        return "Healing";
    case SfxId::ShieldRecharge:
        return "ShieldRecharge";
    case SfxId::ChargeRifleLoad:
        return "ChargeRifleLoad";
    case SfxId::ChargeRifleShoot:
        return "ChargeRifleShoot";
    case SfxId::EnergyBeamLoop:
        return "EnergyBeamLoop";
    case SfxId::FootstepLight:
        return "FootstepLight";
    case SfxId::FootstepHeavy:
        return "FootstepHeavy";
    case SfxId::GrenadeThrow:
        return "GrenadeThrow";
    case SfxId::VoiceStart:
        return "VoiceStart";
    case SfxId::VoiceStop:
        return "VoiceStop";
    default:
        return "Unknown";
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

    loadClip(SfxId::RifleFire, "pubg-ak.wav", SfxCategory::Weapons, 0.8f, 0.10f);
    loadClip(SfxId::RocketFire, "Voicy_Minecraft TNT Explosion.mp3", SfxCategory::Weapons, 0.7f, 0.80f);
    loadClip(SfxId::RailGunFire, "Voicy_Charge Rifle SFX.mp3", SfxCategory::Weapons, 0.8f, 0.50f);
    loadClip(SfxId::EnergyGunFire, "Voicy_Charge Rifle SFX.mp3", SfxCategory::Weapons, 0.5f, 0.08f);
    loadClip(SfxId::ChargeRifleLoad, "charge-rifle-load.wav", SfxCategory::Weapons, 0.9f, 0.0f);
    loadClip(SfxId::ChargeRifleShoot, "charge-rifle-shoot.wav", SfxCategory::Weapons, 1.0f, 0.20f);
    loadClip(SfxId::EnergyBeamLoop, "Voicy_Thunderstruck into.mp3", SfxCategory::Weapons, 0.6f, 0.0f);

    loadClip(SfxId::FleshHit, "Voicy_Flesh Bullet Impact SFX.mp3", SfxCategory::Impacts, 0.7f, 0.08f);
    loadClip(SfxId::Headshot, "Voicy_Headshot Rapid SFX.mp3", SfxCategory::Impacts, 0.8f, 0.08f);
    loadClip(SfxId::Explosion, "Voicy_Minecraft TNT Explosion.mp3", SfxCategory::Impacts, 1.0f, 0.30f);

    loadClip(SfxId::DamageTaken, "Voicy_roblox ooof.mp3", SfxCategory::Player, 0.8f, 0.30f);
    loadClip(SfxId::ArmorBreak, "Voicy_Fortnite Shield Break.mp3", SfxCategory::Player, 0.9f, 1.00f);
    loadClip(SfxId::Death, "Voicy_Minecraft Death Sound.mp3", SfxCategory::Player, 1.0f, 2.00f);
    loadClip(SfxId::Respawn, "Voicy_totem of undying sfx .mp3", SfxCategory::Player, 0.8f, 2.00f);
    loadClip(SfxId::KillConfirm, "Voicy_Pilot Killed Indicator SFX.mp3", SfxCategory::Player, 0.9f, 0.30f);
    loadClip(SfxId::Healing, "Voicy_Syringe SFX .mp3", SfxCategory::Player, 0.5f, 1.00f);
    loadClip(SfxId::ShieldRecharge, "Voicy_Halo Shield Recharge.mp3", SfxCategory::Player, 0.5f, 1.00f);

    synthesizeClip(SfxId::FootstepLight, SfxCategory::Footsteps, 0.40f, 0.06f);
    synthesizeClip(SfxId::FootstepHeavy, SfxCategory::Footsteps, 0.55f, 0.06f);
    synthesizeClip(SfxId::GrenadeThrow, SfxCategory::Weapons, 0.45f, 0.12f);
    synthesizeClip(SfxId::VoiceStart, SfxCategory::Voice, 0.20f, 0.05f);
    synthesizeClip(SfxId::VoiceStop, SfxCategory::Voice, 0.14f, 0.05f);

    convertClipsToMixer();
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
    if (!mixStream_)
        return kInvalidSource;

    const size_t idx = static_cast<size_t>(id);
    if (idx >= clips_.size())
        return kInvalidSource;
    const SoundClip& clip = clips_[idx];
    if (!clip.loaded || clip.samples.empty())
        return kInvalidSource;
    if (cooldowns_[idx] > 0.0f)
        return kInvalidSource;
    cooldowns_[idx] = clip.minCooldown;

    std::lock_guard lock(mixerMutex_);
    Source* source = acquireSource(priority);
    if (!source)
        return kInvalidSource;

    const SourceHandle handle = nextSourceHandle_++;
    if (handle == kInvalidSource)
        nextSourceHandle_ = 1;
    *source = Source{};
    source->active = true;
    source->playingId = id;
    source->handle = handle;
    source->gain = gain;
    source->priority = priority;
    return source->handle;
}

SfxSystem::SourceHandle
SfxSystem::play3D(SfxId id, const glm::vec3& position, const glm::vec3& velocity, float gain, float priority)
{
    SourceHandle handle = play2D(id, gain, priority);
    std::lock_guard lock(mixerMutex_);
    Source* source = findSource(handle);
    if (!source)
        return kInvalidSource;
    source->positional = true;
    source->position = position;
    source->velocity = velocity;
    source->occluded = isOccluded(position);
    return handle;
}

SfxSystem::SourceHandle
SfxSystem::startLoop(SfxId id, bool positional, const glm::vec3& position, float gain, float priority)
{
    SourceHandle handle = play2D(id, gain, priority);
    std::lock_guard lock(mixerMutex_);
    Source* source = findSource(handle);
    if (!source)
        return kInvalidSource;
    source->loop = true;
    source->positional = positional;
    source->position = position;
    source->occluded = positional && isOccluded(position);
    return handle;
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
    *source = Source{};
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
        source = acquireSource(3.0f);
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
        source->gain = 1.0f;
        source->priority = 3.0f;
        voiceSources_[speaker.value] = handle;
    } else if (source->hasVoiceSeq && !sequenceNewer(sequence, source->newestVoiceSeq)) {
        return;
    }

    source->newestVoiceSeq = sequence;
    source->hasVoiceSeq = true;
    source->age = 0.0f;
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

float SfxSystem::categoryVolume(SfxCategory cat) const
{
    return categoryVolumes_[static_cast<size_t>(cat)];
}

void SfxSystem::onWeaponFired(const WeaponFiredEvent& e)
{
    switch (e.type) {
    case WeaponType::Rifle:
        play3D(SfxId::RifleFire, e.origin, glm::vec3{0.0f}, 1.0f, 2.0f);
        break;
    case WeaponType::Rocket:
        play3D(SfxId::GrenadeThrow, e.origin, glm::vec3{0.0f}, 0.9f, 1.5f);
        break;
    case WeaponType::RailGun:
        play3D(SfxId::ChargeRifleShoot, e.origin, glm::vec3{0.0f}, 1.0f, 2.0f);
        break;
    case WeaponType::EnergyGun:
    case WeaponType::HEGrenade:
    case WeaponType::Molotov:
    case WeaponType::Impulse:
        break;
    }
}

void SfxSystem::onExplosion(const ExplosionEvent& e)
{
    play3D(SfxId::Explosion, e.pos, glm::vec3{0.0f}, std::clamp(e.blastRadius / 220.0f, 0.75f, 1.8f), 3.0f);
}

void SfxSystem::handleEvent(const SDL_Event& event)
{
#ifdef __APPLE__
    if (event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED && !event.adevice.recording) {
        if (physicalDeviceId_ != 0 && event.adevice.which == physicalDeviceId_) {
            SDL_Log("[sfx] Playback device removed (id=%u), scheduling reopen",
                    static_cast<unsigned>(physicalDeviceId_));
            pendingReopenDeviceId_ = 0;
            pendingReopen_ = true;
        }
    } else if (event.type == SDL_EVENT_AUDIO_DEVICE_ADDED && !event.adevice.recording) {
        SDL_Log("[sfx] New playback device available (id=%u), scheduling switch",
                static_cast<unsigned>(event.adevice.which));
        pendingReopenDeviceId_ = event.adevice.which;
        pendingReopen_ = true;
    }
#else
    (void)event;
#endif
}

void SfxSystem::update(float dt, const Registry& registry)
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
            }
        }
    }

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
            play2D(SfxId::Death);
            play2D(SfxId::Respawn);
        } else {
            const bool healthLost = h.health < prevHealth_ || h.armor < prevArmor_;
            const bool armorJustBroke = prevArmor_ > 0.0f && h.armor <= 0.0f;
            if (healthLost)
                play2D(SfxId::DamageTaken);
            if (armorJustBroke)
                play2D(SfxId::ArmorBreak);
            const bool healing = h.health > prevHealth_ || h.armor > prevArmor_;
            if (healing && healingSoundCooldown_ <= 0.0f) {
                play2D(SfxId::Healing);
                healingSoundCooldown_ = 1.0f;
            }
        }

        if (stats.kills > prevKills_)
            play2D(SfxId::KillConfirm);

        prevHealth_ = h.health;
        prevArmor_ = h.armor;
        prevDeaths_ = stats.deaths;
        prevKills_ = stats.kills;
        break;
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
    const int frames = id == SfxId::FootstepHeavy ? 4200 : 2800;
    clip.samples.assign(static_cast<std::size_t>(frames) * 2u, 0.0f);
    std::uint32_t seed = 0x1234567u + static_cast<std::uint32_t>(id) * 101u;
    for (int i = 0; i < frames; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float noise = (static_cast<float>((seed >> 9u) & 0xffffu) / 32767.5f) - 1.0f;
        const float t = static_cast<float>(i) / static_cast<float>(frames);
        const float env = std::exp(-t * (id == SfxId::FootstepHeavy ? 8.0f : 12.0f));
        const float tone = std::sin(2.0f * kPi * (id == SfxId::GrenadeThrow ? 220.0f : 95.0f) *
                                    (static_cast<float>(i) / static_cast<float>(audio::k_mixerSampleRate)));
        const float sample = (noise * 0.55f + tone * 0.2f) * env * 0.32f;
        clip.samples[static_cast<std::size_t>(i) * 2u] = sample;
        clip.samples[static_cast<std::size_t>(i) * 2u + 1u] = sample;
    }
    clip.spec = mixerSpec_;
    clip.frameCount = static_cast<std::size_t>(frames);
    clip.durationSeconds = static_cast<float>(frames) / static_cast<float>(audio::k_mixerSampleRate);
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

SfxSystem::Source* SfxSystem::acquireSource(float priority)
{
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
    SDL_AudioDeviceID targetPhysical = pendingReopenDeviceId_;
    if (!targetPhysical) {
        int count = 0;
        SDL_AudioDeviceID* devs = SDL_GetAudioPlaybackDevices(&count);
        if (devs && count > 0) {
            targetPhysical = devs[0];
            SDL_free(devs);
        }
    }
    physicalDeviceId_ = targetPhysical;
    mixStream_ = SDL_OpenAudioDeviceStream(targetPhysical ? targetPhysical : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                           &mixerSpec_,
                                           &SfxSystem::mixCallback,
                                           this);
#else
    mixStream_ =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &mixerSpec_, &SfxSystem::mixCallback, this);
#endif

    if (!mixStream_) {
        SDL_Log("[sfx] SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        return false;
    }
    device_ = SDL_GetAudioStreamDevice(mixStream_);
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
        SDL_Log("[sfx] Reopened audio on physical device %u (logical %u)",
                static_cast<unsigned>(physicalDeviceId_),
                static_cast<unsigned>(device_));
    } else {
        SDL_Log("[sfx] No audio device available after reopen — running mute");
    }
    pendingReopenDeviceId_ = 0;
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
                spatial = audio::evaluateSpatial(source.position, source.velocity, listener_, source.occluded);
            if (!spatial.audible)
                continue;

            const float baseGain =
                source.voiceStream
                    ? masterVolume_ * categoryVolumes_[static_cast<size_t>(SfxCategory::Voice)] * source.gain
                    : effectiveGain(source.playingId, source.gain);
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
