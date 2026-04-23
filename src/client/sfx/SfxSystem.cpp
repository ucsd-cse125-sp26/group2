/// @file SfxSystem.cpp
/// @brief Client-side SFX system implementation.

#include "SfxSystem.hpp"

#include "ecs/components/Health.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerMatchStats.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <string>

// ---------------------------------------------------------------------------
// minimp3 — header-only MP3 decoder (implementation defined exactly once here)
// Diagnostic suppression keeps compiler warnings from minimp3's C code out of
// our build output.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

/// @brief Map SfxId to a human-readable name for logging.
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
    default:
        return "Unknown";
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool SfxSystem::init()
{
    // Initialize the audio subsystem (additive — SDL_Init(VIDEO) already ran).
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("[sfx] SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
        return false;
    }

    // Open the default playback device.
    // Passing nullptr for the spec lets SDL use the device's native format;
    // each AudioStream performs per-stream format conversion automatically.
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device_) {
        SDL_Log("[sfx] SDL_OpenAudioDevice failed: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    // All category volumes start at 1.0 (full).
    categoryVolumes_.fill(1.0f);

    // All per-sound cooldowns start at 0 (ready to play immediately).
    cooldowns_.fill(0.0f);

    // ------------------------------------------------------------------
    // Load sound clips.
    // File names must match exactly what's in assets/sounds/.
    // loadClip() is non-fatal: if a file is missing, that slot stays empty
    // and play() silently skips it.
    // ------------------------------------------------------------------

    // Weapons — fire sounds
    loadClip(SfxId::RifleFire, "pubg-ak.wav", SfxCategory::Weapons, 0.8f, 0.10f);
    loadClip(SfxId::RocketFire, "Voicy_Minecraft TNT Explosion.mp3", SfxCategory::Weapons, 0.7f, 0.80f);
    loadClip(SfxId::RailGunFire, "Voicy_Charge Rifle SFX.mp3", SfxCategory::Weapons, 0.8f, 0.50f);
    loadClip(SfxId::EnergyGunFire, "Voicy_Charge Rifle SFX.mp3", SfxCategory::Weapons, 0.5f, 0.08f);

    // Impacts / hitmarkers
    loadClip(SfxId::FleshHit, "Voicy_Flesh Bullet Impact SFX.mp3", SfxCategory::Impacts, 0.7f, 0.08f);
    loadClip(SfxId::Headshot, "Voicy_Headshot Rapid SFX.mp3", SfxCategory::Impacts, 0.8f, 0.08f);
    loadClip(SfxId::Explosion, "Voicy_Minecraft TNT Explosion.mp3", SfxCategory::Impacts, 1.0f, 0.30f);

    // Player feedback
    loadClip(SfxId::DamageTaken, "Voicy_roblox ooof.mp3", SfxCategory::Player, 0.8f, 0.30f);
    loadClip(SfxId::ArmorBreak, "Voicy_Fortnite Shield Break.mp3", SfxCategory::Player, 0.9f, 1.00f);
    loadClip(SfxId::Death, "Voicy_Minecraft Death Sound.mp3", SfxCategory::Player, 1.0f, 2.00f);
    loadClip(SfxId::Respawn, "Voicy_totem of undying sfx .mp3", SfxCategory::Player, 0.8f, 2.00f);
    loadClip(SfxId::KillConfirm, "Voicy_Pilot Killed Indicator SFX.mp3", SfxCategory::Player, 0.9f, 0.30f);

    // Healing / shield
    loadClip(SfxId::Healing, "Voicy_Syringe SFX .mp3", SfxCategory::Player, 0.5f, 1.00f);
    loadClip(SfxId::ShieldRecharge, "Voicy_Halo Shield Recharge.mp3", SfxCategory::Player, 0.5f, 1.00f);

    // Report how many clips loaded successfully.
    int loaded = 0;
    for (const auto& clip : clips_)
        if (clip.loaded)
            ++loaded;

    SDL_Log("[sfx] SfxSystem initialized — %d/%d clips loaded, device id=%u",
            loaded,
            static_cast<int>(SfxId::_Count),
            static_cast<unsigned>(device_));

    return true;
}

void SfxSystem::quit()
{
    if (!device_)
        return;

    // Destroy all active audio streams (SDL3 auto-unbinds on destroy).
    for (Voice& v : voices_) {
        if (v.stream) {
            SDL_UnbindAudioStream(v.stream);
            SDL_DestroyAudioStream(v.stream);
            v.stream = nullptr;
        }
        v.active = false;
    }

    SDL_CloseAudioDevice(device_);
    device_ = 0;

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_Log("[sfx] SfxSystem shut down");
}

void SfxSystem::play(SfxId id, float gain)
{
    if (!device_)
        return;

    const size_t idx = static_cast<size_t>(id);
    const SoundClip& clip = clips_[idx];

    if (!clip.loaded || clip.pcmData.empty())
        return;

    // Per-sound spam guard.
    if (cooldowns_[idx] > 0.0f)
        return;
    cooldowns_[idx] = clip.minCooldown;

    // Grab a free voice from the pool (recycles oldest if all busy).
    Voice* voice = acquireVoice();
    if (!voice)
        return; // Should never happen after acquireVoice's recycle path.

    // Create a stream that converts from the clip's format to whatever the
    // device needs.  dst_spec = nullptr → format resolved at BindAudioStream.
    SDL_AudioStream* stream = SDL_CreateAudioStream(&clip.spec, nullptr);
    if (!stream) {
        SDL_Log("[sfx] SDL_CreateAudioStream failed for %s: %s", sfxIdName(id), SDL_GetError());
        return;
    }

    // Bind to the device so SDL mixes this stream with all others automatically.
    if (!SDL_BindAudioStream(device_, stream)) {
        SDL_Log("[sfx] SDL_BindAudioStream failed for %s: %s", sfxIdName(id), SDL_GetError());
        SDL_DestroyAudioStream(stream);
        return;
    }

    // Apply volume (master × category × clip default × caller extra).
    SDL_SetAudioStreamGain(stream, effectiveGain(id, gain));

    // Push all PCM data then signal end-of-clip so SDL knows to stop reading.
    SDL_PutAudioStreamData(stream, clip.pcmData.data(), static_cast<int>(clip.pcmData.size()));
    SDL_FlushAudioStream(stream);

    // Record into the voice pool for lifecycle management.
    voice->stream = stream;
    voice->active = true;
    voice->duration = clip.durationSeconds;
    voice->elapsed = 0.0f;
}

void SfxSystem::setCategoryVolume(SfxCategory cat, float v)
{
    categoryVolumes_[static_cast<size_t>(cat)] = v;
}

float SfxSystem::categoryVolume(SfxCategory cat) const
{
    return categoryVolumes_[static_cast<size_t>(cat)];
}

// ---------------------------------------------------------------------------
// entt::dispatcher event handlers
// ---------------------------------------------------------------------------

void SfxSystem::onWeaponFired(const WeaponFiredEvent& e)
{
    switch (e.type) {
    case WeaponType::Rifle:
        play(SfxId::RifleFire);
        break;
    case WeaponType::Rocket:
        play(SfxId::RocketFire);
        break;
    case WeaponType::RailGun:
        play(SfxId::RailGunFire);
        break;
    case WeaponType::EnergyGun:
        play(SfxId::EnergyGunFire);
        break;
    }
}

void SfxSystem::onExplosion(const ExplosionEvent& /*e*/)
{
    play(SfxId::Explosion);
}

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------

void SfxSystem::update(float dt, const Registry& registry)
{
    if (!device_)
        return;

    // 1. Tick down per-sound cooldowns.
    for (auto& cd : cooldowns_)
        if (cd > 0.0f)
            cd = (cd > dt) ? (cd - dt) : 0.0f;

    // 2. Tick down the healing-sound throttle.
    if (healingSoundCooldown_ > 0.0f)
        healingSoundCooldown_ = (healingSoundCooldown_ > dt) ? (healingSoundCooldown_ - dt) : 0.0f;

    // 3. Retire voices whose expected playback time has elapsed.
    //    A small 0.1 s buffer accounts for device buffer latency.
    for (Voice& v : voices_) {
        if (!v.active)
            continue;
        v.elapsed += dt;
        if (v.elapsed > v.duration + 0.1f) {
            if (v.stream) {
                SDL_UnbindAudioStream(v.stream);
                SDL_DestroyAudioStream(v.stream);
                v.stream = nullptr;
            }
            v.active = false;
        }
    }

    // 4. Detect client-side game-state changes on the local player.
    //
    // Key design constraint: the server's handleDeath() immediately calls
    // handleRespawn() in the same tick, resetting IsDead → false and Health
    // back to 100/100.  The client therefore never sees IsDead == true in a
    // synced registry snapshot.  We detect death by watching the deaths counter
    // in PlayerMatchStats instead.
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

        const bool justDied = (stats.deaths > prevDeaths_);

        if (justDied) {
            // Both death and (immediate server) respawn happened this frame.
            play(SfxId::Death);
            play(SfxId::Respawn);
        } else {
            // Only run damage / heal detection when there was no death-respawn
            // this frame (a respawn resets health to 100, which would otherwise
            // falsely trigger "healing" or suppress the armor-break sound).

            const bool healthLost = (h.health < prevHealth_) || (h.armor < prevArmor_);
            const bool armorJustBroke = (prevArmor_ > 0.0f) && (h.armor <= 0.0f);

            if (healthLost)
                play(SfxId::DamageTaken);

            if (armorJustBroke)
                play(SfxId::ArmorBreak);

            // Healing: health or armor ticked upward.
            // Throttled to 1 play per second so the sound doesn't fire every frame.
            const bool healing = (h.health > prevHealth_) || (h.armor > prevArmor_);
            if (healing && healingSoundCooldown_ <= 0.0f) {
                play(SfxId::Healing);
                healingSoundCooldown_ = 1.0f;
            }
        }

        // Kill confirm: local player's kill count went up.
        if (stats.kills > prevKills_)
            play(SfxId::KillConfirm);

        prevHealth_ = h.health;
        prevArmor_ = h.armor;
        prevDeaths_ = stats.deaths;
        prevKills_ = stats.kills;

        break; // There is exactly one LocalPlayer entity.
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool SfxSystem::loadClip(SfxId id, const char* filename, SfxCategory cat, float gain, float cooldownSecs)
{
    // Build the full path: <SDL_GetBasePath()>/assets/sounds/<filename>
    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "assets/sounds/" + filename;

    SoundClip& clip = clips_[static_cast<size_t>(id)];

    // Dispatch to the appropriate decoder based on file extension.
    const std::string fname = filename;
    const bool isWav = fname.ends_with(".wav") || fname.ends_with(".WAV");

    if (isWav) {
        // --- WAV path: use SDL3's built-in WAV loader ---
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

        // Duration: bytes ÷ (bytes-per-sample × channels × freq).
        // SDL_AUDIO_BITSIZE gives bits per sample; divide by 8 for bytes.
        const int bitsPerSample = SDL_AUDIO_BITSIZE(wavSpec.format);
        const int bytesPerFrame = (bitsPerSample / 8) * wavSpec.channels;
        if (bytesPerFrame > 0 && wavSpec.freq > 0) {
            const size_t frames = wavLen / static_cast<size_t>(bytesPerFrame);
            clip.durationSeconds = static_cast<float>(frames) / static_cast<float>(wavSpec.freq);
        } else {
            clip.durationSeconds = 1.0f;
        }

        SDL_Log("[sfx]   %-45s  %.2fs  %5dHz  %dch  %5u KB  [wav]",
                filename,
                static_cast<double>(clip.durationSeconds),
                wavSpec.freq,
                wavSpec.channels,
                wavLen / 1024u);

    } else {
        // --- MP3 path: use minimp3 ---
        mp3dec_t dec{};
        mp3dec_file_info_t info{};

        const int rc = mp3dec_load(&dec, path.c_str(), &info, nullptr, nullptr);
        if (rc != 0 || info.samples == 0) {
            SDL_Log("[sfx] Failed to load '%s' (rc=%d, path=%s)", filename, rc, path.c_str());
            if (info.buffer)
                free(info.buffer);
            return false;
        }

        // Copy PCM from minimp3's malloc'd buffer into our managed vector.
        const size_t byteCount = info.samples * sizeof(mp3d_sample_t);
        clip.pcmData.assign(reinterpret_cast<const uint8_t*>(info.buffer),
                            reinterpret_cast<const uint8_t*>(info.buffer) + byteCount);
        free(info.buffer);

        // minimp3 always outputs signed 16-bit little-endian interleaved PCM.
        clip.spec.format = SDL_AUDIO_S16LE;
        clip.spec.channels = info.channels;
        clip.spec.freq = info.hz;

        // Duration: total samples ÷ (channels × sample_rate) = seconds.
        if (info.channels > 0 && info.hz > 0) {
            const size_t frames = info.samples / static_cast<size_t>(info.channels);
            clip.durationSeconds = static_cast<float>(frames) / static_cast<float>(info.hz);
        } else {
            clip.durationSeconds = 1.0f;
        }

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

SfxSystem::Voice* SfxSystem::acquireVoice()
{
    // Fast path: find an unused slot.
    for (Voice& v : voices_)
        if (!v.active)
            return &v;

    // All slots are occupied — evict the voice furthest into its playback
    // (the one that has the least audio remaining, i.e. highest elapsed/duration ratio).
    Voice* best = nullptr;
    float bestRatio = -1.0f;
    for (Voice& v : voices_) {
        const float ratio = (v.duration > 0.0f) ? (v.elapsed / v.duration) : 1.0f;
        if (ratio > bestRatio) {
            bestRatio = ratio;
            best = &v;
        }
    }

    if (best && best->stream) {
        SDL_UnbindAudioStream(best->stream);
        SDL_DestroyAudioStream(best->stream);
        best->stream = nullptr;
        best->active = false;
    }

    return best;
}

float SfxSystem::effectiveGain(SfxId id, float extraGain) const
{
    const SoundClip& clip = clips_[static_cast<size_t>(id)];
    return masterVolume_ * categoryVolumes_[static_cast<size_t>(clip.category)] * clip.defaultGain * extraGain;
}
