/// @file SfxSystem.hpp
/// @brief Client-side sound effects system.
///
/// Owns the SDL3 audio device, clip bank, source pool, and custom mixer.
/// Follows the same init / update / quit lifecycle as ParticleSystem.
/// Subscribes to entt::dispatcher events (WeaponFiredEvent, ExplosionEvent)
/// and detects client-side state changes (health, death, kills) each update().

#pragma once

#include "AudioMath.hpp"
#include "AudioRuntime.hpp"
#include "SfxTypes.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/registry/Registry.hpp"
#include "particles/ParticleEvents.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

struct SfxRuntimeStats
{
    std::uint64_t sourcesStarted = 0;
    std::uint64_t droppedByCooldown = 0;
    std::uint64_t droppedByLimit = 0;
    std::uint64_t stolenSources = 0;
    std::uint64_t virtualizedFrames = 0;
};

/// @brief Client-side sound effects system.
///
/// All audio is local to the client — the server never drives sounds directly.
/// Weapon-fire SFX hook into the existing WeaponFiredEvent on the dispatcher.
/// Health/death/kill changes are detected by comparing previous-frame state in
/// update(), because the server's handleDeath() immediately calls handleRespawn()
/// in the same tick so IsDead never survives to a registry sync.
///
/// On macOS, device hot-swap (e.g. plugging/unplugging headphones) is handled
/// via SDL_EVENT_AUDIO_DEVICE_REMOVED / ADDED events which trigger a graceful
/// reopen of the default playback device.
class SfxSystem
{
public:
    using SourceHandle = std::uint32_t;
    static constexpr SourceHandle kInvalidSource = 0;

    /// @brief Initialise SDL audio subsystem, open default playback device, load all clips.
    /// @return True on success; false on any audio-init failure (non-fatal — game continues mute).
    bool init();

    /// @brief Per-frame update: decrement cooldowns and retire finished voices.
    void update(float dt);

    /// @brief Per-frame update: decrement cooldowns, retire finished voices, detect state changes.
    /// @param dt        Frame delta time in seconds.
    /// @param registry  ECS registry (read-only — used to detect health/death/kill changes).
    void update(float dt, const Registry& registry);

    /// @brief Destroy active sources, close the audio device, quit audio subsystem.
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
    SourceHandle play2D(SfxId id, float gain = 1.0f, float priority = 1.0f);
    SourceHandle playUi(UiSoundAction action, float gain = 1.0f);
    SourceHandle play3D(SfxId id,
                        const glm::vec3& position,
                        const glm::vec3& velocity = glm::vec3{0.0f},
                        float gain = 1.0f,
                        float priority = 1.0f);
    SourceHandle startLoop(SfxId id,
                           bool positional = false,
                           const glm::vec3& position = glm::vec3{0.0f},
                           float gain = 1.0f,
                           float priority = 1.0f);
    void updateSource(SourceHandle handle,
                      const glm::vec3& position,
                      const glm::vec3& velocity = glm::vec3{0.0f},
                      float gain = 1.0f);
    void stopSource(SourceHandle handle);
    void playMusic(SfxId id, float gain = 1.0f);
    void stopMusic();
    void setListener(const audio::ListenerState& listener);
    void setAudioObjectTransform(audio::AudioObjectId object,
                                 const glm::vec3& position,
                                 const glm::vec3& velocity = glm::vec3{0.0f});
    void removeAudioObject(audio::AudioObjectId object);
    void setAudioRtpc(audio::AudioObjectId object, audio::RtpcId rtpc, float value);
    void setAudioSwitch(audio::AudioObjectId object, audio::SwitchGroupId group, audio::SwitchValueId value);
    void setAudioState(audio::StateGroupId group, audio::StateValueId value);
    void setAudioBusVolume(audio::AudioBusId bus, float volume);
    bool reloadAudioManifest();
    SourceHandle
    postAudioEvent(std::string_view eventName, audio::AudioObjectId object = audio::kGlobalObject, float gain = 1.0f);
    SourceHandle postLocalAudioEvent(std::string_view eventName,
                                     audio::AudioObjectId object = audio::kGlobalObject,
                                     float gain = 1.0f);
    void submitVoiceFrame(ClientId speaker,
                          std::uint16_t sequence,
                          std::span<const float> monoPcm48k,
                          const glm::vec3& position,
                          const glm::vec3& velocity = glm::vec3{0.0f});

    /// @brief Stop all active voices playing the given sound.
    void stop(SfxId id);

    // --- Volume control ---
    void setMasterVolume(float v) { masterVolume_ = v; }
    void setCategoryVolume(SfxCategory cat, float v);
    void setPlaybackDeviceName(std::string_view name);
    float masterVolume() const { return masterVolume_; }
    float categoryVolume(SfxCategory cat) const;

    // --- entt::dispatcher event handlers ---
    void onWeaponFired(const WeaponFiredEvent& e);
    void onExplosion(const ExplosionEvent& e);

    /// @brief True after a successful init().
    bool isInitialized() const { return device_ != 0; }
    [[nodiscard]] const audio::AudioRuntime& audioRuntime() const noexcept { return audioRuntime_; }
    [[nodiscard]] const audio::AudioRuntimeStats& audioStats() const noexcept { return audioRuntime_.stats(); }
    [[nodiscard]] const SfxRuntimeStats& sfxStats() const noexcept { return sfxStats_; }
    [[nodiscard]] float clipDuration(SfxId id) const noexcept;
    [[nodiscard]] std::uint32_t activeSourceCount() const noexcept;
    [[nodiscard]] std::uint32_t activeVoiceSourceCount() const noexcept;

private:
    SDL_AudioDeviceID device_ = 0;           ///< Logical playback device (0 = not initialised).
    SDL_AudioDeviceID physicalDeviceId_ = 0; ///< Logical device ID we opened (for event matching).
    SDL_AudioStream* mixStream_ = nullptr;   ///< Single SDL stream that receives our mixed stereo output.
    SDL_AudioSpec mixerSpec_{};

    std::array<SoundClip, static_cast<size_t>(SfxId::_Count)> clips_;

    struct Source
    {
        bool active = false;
        bool loop = false;
        bool positional = false;
        bool voiceStream = false;
        SfxId playingId = SfxId::_Count; ///< Which sound this voice is playing.
        SourceHandle handle = kInvalidSource;
        ClientId speaker{};
        float cursor = 0.0f;
        float gain = 1.0f;
        float priority = 1.0f;
        audio::AudioBusId bus{};
        float busGain = 1.0f;
        std::uint16_t maxInstances = 0;
        std::uint16_t maxBusInstances = 0;
        float fullGainDistance = audio::k_fullGainDistance;
        float silentDistance = audio::k_silentDistance;
        float age = 0.0f;
        float lowPassStateL = 0.0f;
        float lowPassStateR = 0.0f;
        std::uint16_t newestVoiceSeq = 0;
        bool hasVoiceSeq = false;
        bool occluded = false;
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        std::vector<float> voicePcm;
        std::size_t voiceReadFrame = 0;
    };
    static constexpr int kMaxSources = 64;
    static constexpr std::size_t kMaxVoiceQueuedFrames = 48000 / 2; // 500 ms per speaker.
    std::array<Source, kMaxSources> sources_{};
    SourceHandle nextSourceHandle_ = 1;
    std::unordered_map<int, SourceHandle> voiceSources_;
    SourceHandle musicHandle_ = kInvalidSource;
    SfxId currentMusic_ = SfxId::_Count;

    float masterVolume_ = 0.8f;
    std::array<float, static_cast<size_t>(SfxCategory::_Count)> categoryVolumes_{};
    audio::AudioRuntime audioRuntime_;
    SfxRuntimeStats sfxStats_{};
    std::string manifestPath_;
    std::string playbackDeviceName_;
    audio::ListenerState listener_{};
    std::array<float, 48000> reverbDelayL_{};
    std::array<float, 48000> reverbDelayR_{};
    std::size_t reverbWrite_ = 0;
    float deathMuffleTarget_ = 0.0f;
    float deathMuffleAmount_ = 0.0f;
    float deathMuffleStateL_ = 0.0f;
    float deathMuffleStateR_ = 0.0f;
    mutable std::mutex mixerMutex_;

    /// @brief Per-SfxId countdown to next allowed play (seconds remaining).
    std::array<float, static_cast<size_t>(SfxId::_Count)> cooldowns_{};
    std::array<float, static_cast<size_t>(UiSoundAction::_Count)> uiActionCooldowns_{};
    std::array<std::size_t, static_cast<size_t>(UiSoundAction::_Count)> uiActionVariantCursors_{};

    // --- Client-side state tracking for event detection ---
    float prevHealth_ = 100.0f;
    float prevArmor_ = 100.0f;
    int prevDeaths_ = 0;
    int prevKills_ = 0;
    bool prevLocalReloading_ = false;
    bool prevLocalRailgunCharging_ = false;
    std::unordered_map<entt::entity, float> prevGrenadeCooldowns_;
    std::unordered_map<entt::entity, bool> knownFireFields_;
    float healingSoundCooldown_ = 0.0f; ///< Throttle the looping heal tick sound.
    bool stateInitialized_ = false;     ///< Skip sounds on the very first update().

    bool pendingReopen_ = false;        ///< Set by handleEvent(), processed at the start of update().

    /// @brief Decode a single MP3 from assets/sounds/ and store it as clip[id].
    bool loadClip(SfxId id, const char* filename, SfxCategory cat, float gain, float cooldownSecs);

    Source* acquireSource(float priority,
                          SfxId id = SfxId::_Count,
                          audio::AudioBusId bus = audio::kInvalidBus,
                          std::uint16_t maxInstances = 0,
                          std::uint16_t maxBusInstances = 0);
    Source* findSource(SourceHandle handle);
    Source* findVoiceSource(ClientId speaker);
    SourceHandle startSource(SfxId id,
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
                             float cooldownOverrideSeconds = -1.0f,
                             float fullGainDistance = audio::k_fullGainDistance,
                             float silentDistance = audio::k_silentDistance);
    SourceHandle playCommand(const audio::AudioCommand& command);

    /// @brief master × category × clip × extraGain.
    float effectiveGain(SfxId id, float extraGain) const;
    void convertClipToMixer(SoundClip& clip, const char* debugName);
    void synthesizeClip(SfxId id, SfxCategory cat, float gain, float cooldownSecs);
    bool isOccluded(const glm::vec3& position) const;
    static void mixCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount);
    void mixIntoStream(SDL_AudioStream* stream, int additionalAmount);

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

    void convertClipsToMixer();
};
