/// @file SfxSystem.cpp
/// @brief Client-side SFX system implementation.

#include "SfxSystem.hpp"

#include "ecs/components/Health.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerMatchStats.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
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

// Helpers

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

// Public API

bool SfxSystem::init()
{
    // Initialize the audio subsystem (additive — SDL_Init(VIDEO) already ran).
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("[sfx] SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
        return false;
    }

    if (!openDevice()) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    // Prime the audio pipeline so the first real sound doesn't glitch.
    warmUpDevice();

    // All category volumes start at 1.0 (full).
    categoryVolumes_.fill(1.0f);

    // All per-sound cooldowns start at 0 (ready to play immediately).
    cooldowns_.fill(0.0f);

    // Load sound clips.
    // File names must match exactly what's in assets/sounds/.
    // loadClip() is non-fatal: if a file is missing, that slot stays empty
    // and play() silently skips it.

    // Weapons — fire sounds
    loadClip(SfxId::RifleFire, "pubg-ak.wav", SfxCategory::Weapons, 0.8f, 0.10f);
    loadClip(SfxId::RocketFire, "Voicy_Minecraft TNT Explosion.mp3", SfxCategory::Weapons, 0.7f, 0.80f);
    loadClip(SfxId::RailGunFire, "Voicy_Charge Rifle SFX.mp3", SfxCategory::Weapons, 0.8f, 0.50f);
    loadClip(SfxId::EnergyGunFire, "Voicy_Charge Rifle SFX.mp3", SfxCategory::Weapons, 0.5f, 0.08f);
    loadClip(SfxId::ChargeRifleLoad, "charge-rifle-load.wav", SfxCategory::Weapons, 0.9f, 0.0f);
    loadClip(SfxId::ChargeRifleShoot, "charge-rifle-shoot.wav", SfxCategory::Weapons, 1.0f, 0.20f);
    loadClip(SfxId::EnergyBeamLoop, "Voicy_Thunderstruck into.mp3", SfxCategory::Weapons, 0.6f, 0.0f);

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

    // Pre-convert all clips to the device's native format so that each
    // AudioStream in the callback is a straight memcpy (no resampling).
    preconvertClips();

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
    voice->playingId = id;
    voice->duration = clip.durationSeconds;
    voice->elapsed = 0.0f;
}

void SfxSystem::stop(SfxId id)
{
    for (Voice& v : voices_) {
        if (v.active && v.playingId == id) {
            if (v.stream) {
                SDL_UnbindAudioStream(v.stream);
                SDL_DestroyAudioStream(v.stream);
                v.stream = nullptr;
            }
            v.active = false;
        }
    }
}

void SfxSystem::setCategoryVolume(SfxCategory cat, float v)
{
    categoryVolumes_[static_cast<size_t>(cat)] = v;
}

float SfxSystem::categoryVolume(SfxCategory cat) const
{
    return categoryVolumes_[static_cast<size_t>(cat)];
}

// entt::dispatcher event handlers

void SfxSystem::onWeaponFired(const WeaponFiredEvent& e)
{
    switch (e.type) {
    case WeaponType::Rifle:
        play(SfxId::RifleFire);
        break;
    case WeaponType::Rocket:
        // No fire sound — the TNT explosion sound plays on detonation
        // via onExplosion(), not on launch.
        break;
    case WeaponType::RailGun:
        play(SfxId::ChargeRifleShoot);
        break;
    case WeaponType::EnergyGun:
        // Beam weapon — sound is handled in Game::iterate() via BeamState.
        break;
    case WeaponType::HEGrenade:
    case WeaponType::Molotov:
    case WeaponType::Impulse:
        break;
    }
}

void SfxSystem::onExplosion(const ExplosionEvent& /*e*/)
{
    play(SfxId::Explosion);
}

// Per-frame update

void SfxSystem::handleEvent(const SDL_Event& event)
{
#ifdef __APPLE__
    // On macOS we opened a specific physical device (see openDevice()), so
    // SDL3 does NOT auto-switch when the default output changes.  We handle
    // the transition ourselves on the next update() (main-thread safe).
    if (event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED && !event.adevice.recording) {
        if (physicalDeviceId_ != 0 && event.adevice.which == physicalDeviceId_) {
            SDL_Log("[sfx] Playback device removed (id=%u), scheduling reopen",
                    static_cast<unsigned>(physicalDeviceId_));
            pendingReopenDeviceId_ = 0; // pick first available
            pendingReopen_ = true;
        }
    } else if (event.type == SDL_EVENT_AUDIO_DEVICE_ADDED && !event.adevice.recording) {
        // A new playback device appeared (headphones plugged in, BT connected,
        // etc.).  On macOS the system default output follows the newly connected
        // device, so switching to it mirrors expected OS behaviour.
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
    // Process any pending device reopen before anything else.
    if (pendingReopen_) {
        pendingReopen_ = false;
        reopenDevice();
    }

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

// Private helpers

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

// Device management — open / reopen / warm-up

bool SfxSystem::openDevice()
{
#ifdef __APPLE__
    // On macOS, avoid SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK.
    //
    // SDL3's CoreAudio backend installs an AudioObjectPropertyListener for the
    // system default output device.  When the default changes (headphones
    // plugged/unplugged, Bluetooth connect/disconnect), the listener fires on
    // CoreAudio's dispatch-queue thread and calls SDL_DefaultAudioDeviceChanged
    // → OpenPhysicalAudioDevice → COREAUDIO_OpenDevice.  A new AudioQueue
    // playback thread starts, but a race between the notification handler and
    // the new thread's AllocateBuffer call leads to a use-after-free (PAC
    // failure → SIGSEGV on Apple Silicon).
    //
    // Opening a *specific* physical device instead of the default logical
    // device prevents SDL from registering that listener entirely.  We handle
    // device removal / addition ourselves via handleEvent().

    // Request larger audio buffers on macOS (2048 frames ≈ 42 ms at 48 kHz).
    // The default 1024 frames gives only ~21 ms per callback.  With many
    // concurrent voices the callback can miss its deadline under Low Power
    // Mode, causing pops.  2048 frames doubles the headroom while keeping
    // latency well under 50 ms (acceptable for game SFX).
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "2048");

    SDL_AudioDeviceID targetPhysical = pendingReopenDeviceId_;
    if (!targetPhysical) {
        // No specific target — pick the first available playback device.
        int count = 0;
        SDL_AudioDeviceID* devs = SDL_GetAudioPlaybackDevices(&count);
        if (devs && count > 0) {
            targetPhysical = devs[0];
            SDL_free(devs);
        }
    }

    if (targetPhysical) {
        physicalDeviceId_ = targetPhysical;
        device_ = SDL_OpenAudioDevice(physicalDeviceId_, nullptr);
    }
    if (!device_) {
        SDL_Log("[sfx] SDL_OpenAudioDevice failed (macOS specific-device path): %s", SDL_GetError());
        return false;
    }
#else
    // On Linux / Windows the default-device auto-switch works reliably.
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device_) {
        SDL_Log("[sfx] SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return false;
    }
#endif

    return true;
}

void SfxSystem::reopenDevice()
{
    SDL_Log("[sfx] Reopening audio device…");

    // 1. Tear down all active voices (SDL_DestroyAudioStream auto-unbinds).
    for (Voice& v : voices_) {
        if (v.stream) {
            SDL_DestroyAudioStream(v.stream);
            v.stream = nullptr;
        }
        v.active = false;
    }

    // 2. Close the old logical device.
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    physicalDeviceId_ = 0;

    // 3. Try to open the requested (or first available) device.
    if (openDevice()) {
        warmUpDevice();
        // Re-convert clips to the new device's native format so the audio
        // callback stays conversion-free on the new device too.
        preconvertClips();
        SDL_Log("[sfx] Reopened audio on physical device %u (logical %u)",
                static_cast<unsigned>(physicalDeviceId_),
                static_cast<unsigned>(device_));
    } else {
        SDL_Log("[sfx] No audio device available after reopen — running mute");
    }

    pendingReopenDeviceId_ = 0;

    // Reset state tracking so the first update after reopen doesn't fire
    // spurious sounds from stale health/death deltas.
    stateInitialized_ = false;
}

void SfxSystem::warmUpDevice()
{
    if (!device_)
        return;

    // Force CoreAudio's AudioQueue to pre-allocate its internal buffer pool
    // and sample-rate converter by binding a tiny silent stream.  Without
    // this the first real sound triggers lazy allocation, which on macOS
    // causes an audible latency spike (stutter / pop / "earrape").
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = 1;
    spec.freq = 44100;

    SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, nullptr);
    if (!stream)
        return;

    if (!SDL_BindAudioStream(device_, stream)) {
        SDL_DestroyAudioStream(stream);
        return;
    }

    // ~23 ms of silence at 44.1 kHz mono S16LE = 1024 frames = 2048 bytes.
    constexpr int kWarmupBytes = 2048;
    uint8_t silence[kWarmupBytes] = {};
    SDL_PutAudioStreamData(stream, silence, kWarmupBytes);
    SDL_FlushAudioStream(stream);

    // Brief pause so the audio thread runs at least one callback cycle,
    // completing the lazy buffer allocation before any real sound arrives.
    SDL_Delay(50);

    SDL_DestroyAudioStream(stream);
    SDL_Log("[sfx] Audio pipeline warmed up");
}

void SfxSystem::preconvertClips()
{
    if (!device_)
        return;

    // Query what the device actually wants.
    SDL_AudioSpec deviceSpec{};
    if (!SDL_GetAudioDeviceFormat(device_, &deviceSpec, nullptr)) {
        SDL_Log("[sfx] Could not query device format, skipping preconvert: %s", SDL_GetError());
        return;
    }

    int converted = 0;
    for (size_t i = 0; i < clips_.size(); ++i) {
        SoundClip& clip = clips_[i];
        if (!clip.loaded || clip.pcmData.empty())
            continue;

        // Already matches — nothing to do.
        if (clip.spec.format == deviceSpec.format && clip.spec.channels == deviceSpec.channels &&
            clip.spec.freq == deviceSpec.freq)
        {
            continue;
        }

        Uint8* dstBuf = nullptr;
        int dstLen = 0;
        if (!SDL_ConvertAudioSamples(
                &clip.spec, clip.pcmData.data(), static_cast<int>(clip.pcmData.size()), &deviceSpec, &dstBuf, &dstLen))
        {
            SDL_Log("[sfx] preconvert failed for clip %zu: %s", i, SDL_GetError());
            continue;
        }

        clip.pcmData.assign(dstBuf, dstBuf + dstLen);
        SDL_free(dstBuf);
        clip.spec = deviceSpec;

        // Recalculate duration from the new format.
        const int bitsPerSample = static_cast<int>(SDL_AUDIO_BITSIZE(deviceSpec.format));
        const int bytesPerFrame = (bitsPerSample / 8) * static_cast<int>(deviceSpec.channels);
        if (bytesPerFrame > 0 && deviceSpec.freq > 0) {
            const size_t frames = static_cast<size_t>(dstLen) / static_cast<size_t>(bytesPerFrame);
            clip.durationSeconds = static_cast<float>(frames) / static_cast<float>(deviceSpec.freq);
        }

        ++converted;
    }

    SDL_Log("[sfx] Pre-converted %d clips to device format "
            "(%d Hz, %d ch, %s)",
            converted,
            deviceSpec.freq,
            deviceSpec.channels,
            SDL_GetAudioFormatName(deviceSpec.format));
}
