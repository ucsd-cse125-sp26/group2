/// @file SfxSystem.hpp
/// @brief Client-side sound effects system.
///
/// Owns the SDL3 audio device and a pool of AudioStream voices.
/// Follows the same init / update / quit lifecycle as ParticleSystem.
/// Subscribes to entt::dispatcher events (WeaponFiredEvent, ExplosionEvent)
/// and detects client-side state changes (health, death, kills) each update().

#pragma once

#include "SfxTypes.hpp"
#include "ecs/registry/Registry.hpp"
#include "particles/ParticleEvents.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>

/// @brief Client-side sound effects system.
///
/// All audio is local to the client — the server never drives sounds directly.
/// Weapon-fire SFX hook into the existing WeaponFiredEvent on the dispatcher.
/// Health/death/kill changes are detected by comparing previous-frame state in
/// update(), because the server's handleDeath() immediately calls handleRespawn()
/// in the same tick so IsDead never survives to a registry sync.
///
/// On macOS, the system opens a specific physical audio device rather than
/// SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK to avoid a race condition in SDL3's
/// CoreAudio backend that causes a SIGSEGV when the default output changes
/// (e.g. plugging/unplugging headphones).  Device hot-swap is handled
/// manually via SDL_EVENT_AUDIO_DEVICE_REMOVED / ADDED events.
class SfxSystem
{
public:
    /// @brief Initialise SDL audio subsystem, open default playback device, load all clips.
    /// @return True on success; false on any audio-init failure (non-fatal — game continues mute).
    bool init();

    /// @brief Per-frame update: decrement cooldowns, retire finished voices, detect state changes.
    /// @param dt        Frame delta time in seconds.
    /// @param registry  ECS registry (read-only — used to detect health/death/kill changes).
    void update(float dt, const Registry& registry);

    /// @brief Destroy all active streams, close the audio device, quit audio subsystem.
    void quit();

    /// @brief Forward SDL audio-device events so the system can handle hot-swap.
    ///
    /// Call this from Game::event() for SDL_EVENT_AUDIO_DEVICE_ADDED,
    /// SDL_EVENT_AUDIO_DEVICE_REMOVED, and SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED.
    void handleEvent(const SDL_Event& event);

    /// @brief Play a sound immediately.
    /// @param id    Which clip to play.
    /// @param gain  Extra volume multiplier (stacks on top of master × category × clip gain).
    void play(SfxId id, float gain = 1.0f);

    /// @brief Stop all active voices playing the given sound.
    void stop(SfxId id);

    // --- Volume control ---
    void setMasterVolume(float v) { masterVolume_ = v; }
    void setCategoryVolume(SfxCategory cat, float v);
    float masterVolume() const { return masterVolume_; }
    float categoryVolume(SfxCategory cat) const;

    // --- entt::dispatcher event handlers ---
    void onWeaponFired(const WeaponFiredEvent& e);
    void onExplosion(const ExplosionEvent& e);

    /// @brief True after a successful init().
    bool isInitialized() const { return device_ != 0; }

private:
    SDL_AudioDeviceID device_ = 0;           ///< Logical playback device (0 = not initialised).
    SDL_AudioDeviceID physicalDeviceId_ = 0; ///< Physical device ID we opened (for event matching).

    std::array<SoundClip, static_cast<size_t>(SfxId::_Count)> clips_;

    /// @brief A single concurrent playback voice.
    struct Voice
    {
        SDL_AudioStream* stream = nullptr; ///< Bound audio stream (nullptr = slot free).
        bool active = false;
        SfxId playingId = SfxId::_Count;   ///< Which sound this voice is playing.
        float duration = 0.0f;             ///< Expected playback length in seconds.
        float elapsed = 0.0f;              ///< Seconds since playback started.
    };
    static constexpr int kMaxVoices = 32;
    std::array<Voice, kMaxVoices> voices_{};

    float masterVolume_ = 0.8f;
    std::array<float, static_cast<size_t>(SfxCategory::_Count)> categoryVolumes_{};

    /// @brief Per-SfxId countdown to next allowed play (seconds remaining).
    std::array<float, static_cast<size_t>(SfxId::_Count)> cooldowns_{};

    // --- Client-side state tracking for event detection ---
    float prevHealth_ = 100.0f;
    float prevArmor_ = 100.0f;
    int prevDeaths_ = 0;
    int prevKills_ = 0;
    float healingSoundCooldown_ = 0.0f; ///< Throttle the looping heal tick sound.
    bool stateInitialized_ = false;     ///< Skip sounds on the very first update().

    bool pendingReopen_ = false;        ///< Set by handleEvent(), processed at the start of update().

    /// @brief Decode a single MP3 from assets/sounds/ and store it as clip[id].
    bool loadClip(SfxId id, const char* filename, SfxCategory cat, float gain, float cooldownSecs);

    /// @brief Return a free Voice slot, or recycle the oldest active one.
    Voice* acquireVoice();

    /// @brief master × category × clip × extraGain.
    float effectiveGain(SfxId id, float extraGain) const;

    /// @brief Open a specific physical playback device (macOS) or the default (other platforms).
    /// @return True on success.
    bool openDevice();

    /// @brief Tear down all active voices/streams, close the device, then reopen.
    void reopenDevice();

    /// @brief Push a tiny silent buffer to force AudioQueue buffer pre-allocation.
    ///
    /// Without this, the first real sound on macOS triggers lazy buffer allocation
    /// inside CoreAudio's AudioQueue, causing an audible latency spike / glitch.
    void warmUpDevice();
};
