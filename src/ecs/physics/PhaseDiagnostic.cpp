/// @file PhaseDiagnostic.cpp
/// @brief Implementation of the per-tick player physics telemetry.

#include "ecs/physics/PhaseDiagnostic.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <unordered_map>

namespace physics::diag
{

namespace
{

std::atomic<bool> enabledFlag{false};

std::mutex logMutex;
FILE* logFile = nullptr;
bool wroteHeader = false;

std::mutex annotationMutex;
std::unordered_map<entt::entity, std::string> pendingAnnotations;

void openLazily()
{
    if (logFile != nullptr)
        return;

    // Embed the wall-clock start time in the filename so consecutive runs
    // don't trample each other.  Stays in the binary's working directory
    // (next to the executable for both client and server).
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    char nameBuf[64];
    std::tm tmbuf{};
#if defined(_WIN32)
    ::localtime_s(&tmbuf, &t);
#else
    ::localtime_r(&t, &tmbuf);
#endif
    std::strftime(nameBuf, sizeof(nameBuf), "phase-diag-%Y%m%d-%H%M%S.csv", &tmbuf);
    logFile = std::fopen(nameBuf, "w");
    if (logFile == nullptr)
        return;

    // CSV header.  Columns chosen to be greppable by a human and
    // sortable / filterable in any spreadsheet program.
    std::fprintf(
        logFile,
        "tick,entity,posBeforeX,posBeforeY,posBeforeZ,posAfterDepenX,posAfterDepenY,posAfterDepenZ,"
        "posAfterX,posAfterY,posAfterZ,velBeforeX,velBeforeY,velBeforeZ,velAfterX,velAfterY,velAfterZ,"
        "actualDelta,expectedDelta,depenPush,bumpHits,moveMode,wallrunSide,jumpCount,"
        "lastNormalX,lastNormalY,lastNormalZ,flagsHex,grounded,wallrun,sliding,climbing,ledge,"
        "grapple,doubleJumped,gravFlipped,depenCancelled,deepPenetration,bumpExhausted,suspectedPhase,note\n");
    wroteHeader = true;
}

const char* moveModeName(int m)
{
    switch (m) {
    case 0: return "OnFoot";
    case 1: return "Sliding";
    case 2: return "WallRunning";
    case 3: return "Climbing";
    case 4: return "LedgeGrabbing";
    default: return "?";
    }
}

} // namespace

void setEnabled(bool on) noexcept
{
    enabledFlag.store(on, std::memory_order_relaxed);
}

bool isEnabled() noexcept
{
    return enabledFlag.load(std::memory_order_relaxed);
}

void annotate(entt::entity entity, std::string_view label) noexcept
{
    if (!enabledFlag.load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lk(annotationMutex);
    pendingAnnotations[entity] = std::string{label};
}

void consumeAnnotation(entt::entity entity, char (&out)[48]) noexcept
{
    out[0] = 0;
    if (!enabledFlag.load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lk(annotationMutex);
    auto it = pendingAnnotations.find(entity);
    if (it == pendingAnnotations.end())
        return;
    const auto n = std::min(it->second.size(), sizeof(out) - 1);
    std::memcpy(out, it->second.data(), n);
    out[n] = 0;
    pendingAnnotations.erase(it);
}

void recordFrame(const PlayerFrame& f) noexcept
{
    if (!enabledFlag.load(std::memory_order_relaxed))
        return;

    std::lock_guard<std::mutex> lk(logMutex);
    openLazily();
    if (logFile == nullptr)
        return;

    // Monotonic row counter — the caller-supplied `tick` field is unused
    // because we don't have a server tick counter plumbed into the
    // collision kernel.  Row order in the CSV preserves chronology.
    static uint64_t rowIndex = 0;
    const uint64_t tickForRow = (f.tick > 0) ? f.tick : ++rowIndex;

    const glm::vec3 actualDelta = f.posAfter - f.posBefore;
    const glm::vec3 expectedDelta = f.velBefore * (1.0f / 128.0f); // assume 128 Hz; precise dt not exposed here
    const float actualMag = std::sqrt(actualDelta.x * actualDelta.x + actualDelta.y * actualDelta.y +
                                       actualDelta.z * actualDelta.z);
    const float expectedMag = std::sqrt(expectedDelta.x * expectedDelta.x + expectedDelta.y * expectedDelta.y +
                                         expectedDelta.z * expectedDelta.z);

    PhaseFlag flags = f.flags;
    // Auto-flag suspected-phase when actual delta significantly exceeds
    // expected.  The +5u absolute slack handles bump-loop stop-and-go;
    // 2× multiplier catches doubled-or-worse motion.
    if (actualMag > expectedMag * 2.0f + 5.0f)
        flags |= PhaseFlag::SuspectedPhase;

    char note[48] = {0};
    std::strncpy(note, f.note, sizeof(note) - 1);
    if (note[0] == 0) {
        // No caller-supplied note — consume any pending annotation for
        // this entity (set by MovementSystem hooks).
        consumeAnnotation(f.entity, note);
    }

    const uint32_t flagBits = static_cast<uint32_t>(flags);
    std::fprintf(logFile,
                 "%lu,%u,"
                 "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                 "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                 "%.3f,%.3f,%.3f,%d,%s,%d,%d,"
                 "%.4f,%.4f,%.4f,0x%X,"
                 "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
                 "%s\n",
                 static_cast<unsigned long>(tickForRow),
                 entt::to_integral(f.entity),
                 static_cast<double>(f.posBefore.x), static_cast<double>(f.posBefore.y), static_cast<double>(f.posBefore.z),
                 static_cast<double>(f.posAfterDepen.x), static_cast<double>(f.posAfterDepen.y),
                 static_cast<double>(f.posAfterDepen.z),
                 static_cast<double>(f.posAfter.x), static_cast<double>(f.posAfter.y), static_cast<double>(f.posAfter.z),
                 static_cast<double>(f.velBefore.x), static_cast<double>(f.velBefore.y), static_cast<double>(f.velBefore.z),
                 static_cast<double>(f.velAfter.x), static_cast<double>(f.velAfter.y), static_cast<double>(f.velAfter.z),
                 static_cast<double>(actualMag), static_cast<double>(expectedMag),
                 static_cast<double>(f.depenPushDistance),
                 f.bumpHits, moveModeName(f.moveMode), f.wallrunSide, f.jumpCount,
                 static_cast<double>(f.lastHitNormal.x), static_cast<double>(f.lastHitNormal.y),
                 static_cast<double>(f.lastHitNormal.z),
                 flagBits,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::Grounded)) ? 1 : 0,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::WallRunning)) ? 1 : 0,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::Sliding)) ? 1 : 0,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::Climbing)) ? 1 : 0,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::LedgeGrabbing)) ? 1 : 0,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::GrappleActive)) ? 1 : 0,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::DoubleJumped)) ? 1 : 0,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::GravityFlipped)) ? 1 : 0,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::DepenCancelled)) ? 1 : 0,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::DeepPenetration)) ? 1 : 0,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::BumpExhausted)) ? 1 : 0,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::SuspectedPhase)) ? 1 : 0,
                 note);
    std::fflush(logFile);
}

} // namespace physics::diag
