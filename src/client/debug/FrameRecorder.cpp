#include "FrameRecorder.hpp"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

// stb_image_write — single-header PNG encoder.
// STB_IMAGE_WRITE_IMPLEMENTATION must be defined in exactly one translation unit.
// ModelLoader.cpp owns STB_IMAGE_IMPLEMENTATION; we own the write side here.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string makeTimestamp()
{
    std::time_t t = std::time(nullptr);
    std::tm* tm_ptr = std::localtime(&t); // NOLINT(concurrency-mt-unsafe)
    std::ostringstream oss;
    oss << std::put_time(tm_ptr, "%Y%m%d_%H%M%S");
    return oss.str();
}

// ── FrameRecorder ─────────────────────────────────────────────────────────────

void FrameRecorder::startRecording(const std::string& baseDir)
{
    if (recording_)
        stopRecording();

    sessionDir_ = baseDir + "/" + makeTimestamp();
    try {
        std::filesystem::create_directories(sessionDir_);
    } catch (const std::exception& e) {
        SDL_Log("FrameRecorder: cannot create session dir '%s': %s", sessionDir_.c_str(), e.what());
        return;
    }

    startTime_ = static_cast<double>(SDL_GetTicks()) / 1000.0;
    frames_.clear();
    recording_ = true;
    SDL_Log("FrameRecorder: recording started → %s", sessionDir_.c_str());
}

void FrameRecorder::stopRecording()
{
    if (!recording_)
        return;
    recording_ = false;
    writeCSV();
    SDL_Log("FrameRecorder: stopped — %zu frames in %s", frames_.size(), sessionDir_.c_str());
    frames_.clear();
}

void FrameRecorder::recordFrame(const FrameState& state)
{
    if (!recording_)
        return;
    frames_.push_back(state);
}

void FrameRecorder::writeCSV() const
{
    const std::string path = sessionDir_ + "/states.csv";
    std::ofstream out(path);
    if (!out.is_open()) {
        SDL_Log("FrameRecorder: cannot open '%s' for writing", path.c_str());
        return;
    }

    out << "frame,timestamp,tick,"
           "pos_x,pos_y,pos_z,"
           "vel_x,vel_y,vel_z,"
           "yaw,pitch,"
           "eye_x,eye_y,eye_z,"
           "render_yaw,render_pitch,"
           "cube_sx,cube_sy,"
           "model_sx,model_sy,"
           "screenshot\n";

    for (const auto& f : frames_) {
        out << f.frameNumber << ',' << f.timestamp << ',' << f.tickCount << ',' << f.physPos.x << ',' << f.physPos.y
            << ',' << f.physPos.z << ',' << f.physVel.x << ',' << f.physVel.y << ',' << f.physVel.z << ',' << f.yaw
            << ',' << f.pitch << ',' << f.renderEye.x << ',' << f.renderEye.y << ',' << f.renderEye.z << ','
            << f.renderYaw << ',' << f.renderPitch << ',' << f.cubeScreen.x << ',' << f.cubeScreen.y << ','
            << f.modelScreen.x << ',' << f.modelScreen.y << ',' << f.screenshotPath << '\n';
    }

    SDL_Log("FrameRecorder: wrote %s", path.c_str());
}

// ── stbi_write_png wrapper (used by Renderer to save captured frames) ─────────

// Declared in this TU so the linker finds the implementation above.
// Renderer.cpp includes stb_image_write.h as a declaration-only header;
// the definition lives here.
//
// (No additional code needed — stb_image_write.h with STB_IMAGE_WRITE_IMPLEMENTATION
//  already provides stbi_write_png etc. as static/extern functions.)
