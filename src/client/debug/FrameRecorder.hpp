#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

/// @brief Per-frame snapshot written to the recording CSV.
///
/// Every field that is relevant to diagnosing camera/position jitter is captured:
///   - Raw physics state (pos, vel)
///   - Raw mouse input (yaw, pitch)
///   - What was actually sent to the renderer (renderEye / renderYaw / renderPitch)
///   - Derived screen-space positions of the reference cube and the Wraith model
///     (jitter will appear as these values oscillating even when the objects are static)
struct FrameState
{
    uint64_t frameNumber = 0;
    double timestamp = 0.0; ///< Seconds since recording started.
    int tickCount = 0;      ///< Physics-tick counter at this render frame.

    // ── raw physics state ────────────────────────────────────────────────────
    glm::vec3 physPos{0.0f}; ///< pos.value — what physics says.
    glm::vec3 physVel{0.0f}; ///< vel.value — current velocity.

    // ── raw input orientation ────────────────────────────────────────────────
    float yaw = 0.0f;   ///< input.yaw   (accumulated mouse X).
    float pitch = 0.0f; ///< input.pitch (accumulated mouse Y).

    // ── what was sent to Renderer::drawFrame ─────────────────────────────────
    glm::vec3 renderEye{0.0f};
    float renderYaw = 0.0f;
    float renderPitch = 0.0f;

    // ── screen-space (pixels) of world objects — jitter is visible here ───────
    glm::vec2 cubeScreen{0.0f};  ///< Cube centre (0, 32, 400).
    glm::vec2 modelScreen{0.0f}; ///< Wraith model centre (200, 0, 400).

    std::string screenshotPath;  ///< Absolute PNG path, or empty if unavailable.
};

/// @brief Records per-frame state to a timestamped directory on disk.
///
/// Usage:
/// @code
///   recorder.startRecording(baseDir);
///   // each frame:
///   recorder.recordFrame(state);
///   // when done:
///   recorder.stopRecording();  // flushes CSV
/// @endcode
class FrameRecorder
{
public:
    /// @brief Open a new session directory inside @p baseDir.
    void startRecording(const std::string& baseDir);

    /// @brief Flush CSV to disk and close the session.
    void stopRecording();

    /// @brief Append one frame's state.  No-op when not recording.
    void recordFrame(const FrameState& state);

    bool isRecording() const { return recording_; }
    const std::string& sessionDir() const { return sessionDir_; }
    double startTimeSecs() const { return startTime_; }

private:
    bool recording_ = false;
    std::string sessionDir_;
    double startTime_ = 0.0; ///< SDL_GetTicks()/1000 at startRecording().
    std::vector<FrameState> frames_;

    void writeCSV() const;
};
