/// @file PhaseDiagnostic.cpp
/// @brief Implementation of the per-tick player physics telemetry.

#include "ecs/physics/PhaseDiagnostic.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>

namespace physics::diag
{

namespace
{

std::atomic<bool> enabledFlag{false};

std::mutex sessionMutex;
std::string filePrefix = "physics";
std::string sessionStamp;

std::mutex logMutex;
FILE* logFile = nullptr;
bool wroteHeader = false;
uint64_t phaseRowIndex = 0;

std::mutex movementLogMutex;
FILE* movementLogFile = nullptr;
uint64_t movementRowIndex = 0;

std::mutex kccTimingLogMutex;
FILE* kccTimingLogFile = nullptr;
uint64_t kccTimingRowIndex = 0;

std::mutex annotationMutex;
std::unordered_map<entt::entity, std::string> pendingAnnotations;

// Separate mutex / file for the depen-contact trace so the rare deep-contact
// path never serializes on the per-tick frame log.
std::mutex depenLogMutex;
FILE* depenLogFile = nullptr;
uint64_t depenRowIndex = 0;

std::string makeTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    char stampBuf[32];
    std::tm tmbuf{};
#if defined(_WIN32)
    ::localtime_s(&tmbuf, &t);
#else
    ::localtime_r(&t, &tmbuf);
#endif
    std::strftime(stampBuf, sizeof(stampBuf), "%Y%m%d-%H%M%S", &tmbuf);
    return stampBuf;
}

std::string makeLogPath(const char* kind)
{
    std::lock_guard<std::mutex> lk(sessionMutex);
    if (sessionStamp.empty())
        sessionStamp = makeTimestamp();
    return filePrefix + "-" + kind + "-" + sessionStamp + ".csv";
}

void closeLog(FILE*& file) noexcept
{
    if (file != nullptr) {
        std::fflush(file);
        std::fclose(file);
        file = nullptr;
    }
}

void closeAllLogs() noexcept
{
    {
        std::lock_guard<std::mutex> lk(logMutex);
        closeLog(logFile);
        wroteHeader = false;
        phaseRowIndex = 0;
    }
    {
        std::lock_guard<std::mutex> lk(movementLogMutex);
        closeLog(movementLogFile);
        movementRowIndex = 0;
    }
    {
        std::lock_guard<std::mutex> lk(kccTimingLogMutex);
        closeLog(kccTimingLogFile);
        kccTimingRowIndex = 0;
    }
    {
        std::lock_guard<std::mutex> lk(depenLogMutex);
        closeLog(depenLogFile);
        depenRowIndex = 0;
    }
}

void openLazily()
{
    if (logFile != nullptr)
        return;

    const std::string path = makeLogPath("phase-diag");
    logFile = std::fopen(path.c_str(), "w");
    if (logFile == nullptr)
        return;

    // CSV header.  Columns chosen to be greppable by a human and
    // sortable / filterable in any spreadsheet program.
    std::fprintf(logFile,
                 "tick,entity,posBeforeX,posBeforeY,posBeforeZ,posAfterDepenX,posAfterDepenY,posAfterDepenZ,"
                 "posAfterX,posAfterY,posAfterZ,velBeforeX,velBeforeY,velBeforeZ,velAfterX,velAfterY,velAfterZ,"
                 "actualDelta,expectedDelta,depenPush,bumpHits,moveMode,wallrunSide,jumpCount,"
                 "lastNormalX,lastNormalY,lastNormalZ,flagsHex,grounded,wallrun,sliding,climbing,ledge,"
                 "grapple,doubleJumped,gravFlipped,depenCancelled,deepPenetration,bumpExhausted,suspectedPhase,"
                 "invalidState,note\n");
    wroteHeader = true;
}

bool finiteVec3(glm::vec3 v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

void openMovementLazily()
{
    if (movementLogFile != nullptr)
        return;

    const std::string path = makeLogPath("movement-diag");
    movementLogFile = std::fopen(path.c_str(), "w");
    if (movementLogFile == nullptr)
        return;

    std::fprintf(movementLogFile,
                 "row,entity,modeBefore,modeAfter,groundedBefore,groundedAfter,"
                 "posBeforeX,posBeforeY,posBeforeZ,posAfterX,posAfterY,posAfterZ,"
                 "velBeforeX,velBeforeY,velBeforeZ,velAfterX,velAfterY,velAfterZ,"
                 "inputF,inputB,inputL,inputR,inputJump,inputCrouch,inputGrapple,yaw,pitch,"
                 "wallFront,ledgeDetected,groundDistance,"
                 "frontNx,frontNy,frontNz,frontPx,frontPy,frontPz,"
                 "ledgeNx,ledgeNy,ledgeNz,ledgePx,ledgePy,ledgePz,"
                 "climbNx,climbNy,climbNz,storedLedgeNx,storedLedgeNy,storedLedgeNz,"
                 "storedLedgePx,storedLedgePy,storedLedgePz,climbTimer,ledgeHoldTimer,"
                 "flagsHex,invalidState,note\n");
}

void openKccTimingLazily()
{
    if (kccTimingLogFile != nullptr)
        return;

    const std::string path = makeLogPath("kcc-timing");
    kccTimingLogFile = std::fopen(path.c_str(), "w");
    if (kccTimingLogFile == nullptr)
        return;

    std::fprintf(kccTimingLogFile,
                 "row,entity,elapsedUs,substeps,caIterations,clearanceQueries,sweepQueries,sweepHits,"
                 "usedWalkCapsule,caExhausted,grounded,moveMode\n");
}

const char* moveModeName(int m)
{
    switch (m) {
    case 0:
        return "OnFoot";
    case 1:
        return "Sliding";
    case 2:
        return "WallRunning";
    case 3:
        return "Climbing";
    case 4:
        return "LedgeGrabbing";
    default:
        return "?";
    }
}

const char* triRegionName(int r)
{
    switch (r) {
    case 0:
        return "Face";
    case 1:
        return "Edge0";
    case 2:
        return "Edge1";
    case 3:
        return "Edge2";
    case 4:
        return "Vert0";
    case 5:
        return "Vert1";
    case 6:
        return "Vert2";
    default:
        return "?";
    }
}

void openDepenLazily()
{
    if (depenLogFile != nullptr)
        return;
    const std::string path = makeLogPath("depen-trace");
    depenLogFile = std::fopen(path.c_str(), "w");
    if (depenLogFile == nullptr)
        return;
    // depthOverR is depth / R - useful sort key: 2.0 = saturated back-face (the
    // duplicate-triangle / inverted-winding signature); 1.0-1.5 = ordinary
    // sub-tick penetration from sweep clearance.
    std::fprintf(depenLogFile,
                 "row,triId,posX,posY,posZ,faceNx,faceNy,faceNz,"
                 "v0x,v0y,v0z,v1x,v1y,v1z,v2x,v2y,v2z,"
                 "signedDist,minkowskiR,depth,depthOverR,region,edgeFlagsHex,vertFlagsHex\n");
}

} // namespace

void setFilePrefix(std::string_view prefix)
{
    if (prefix.empty())
        return;
    std::lock_guard<std::mutex> lk(sessionMutex);
    filePrefix.assign(prefix.data(), prefix.size());
}

void startRecording()
{
    enabledFlag.store(false, std::memory_order_relaxed);
    closeAllLogs();
    {
        std::lock_guard<std::mutex> lk(sessionMutex);
        sessionStamp = makeTimestamp();
    }
    enabledFlag.store(true, std::memory_order_relaxed);
}

void stopRecording()
{
    enabledFlag.store(false, std::memory_order_relaxed);
    closeAllLogs();
}

void setEnabled(bool on)
{
    if (on)
        startRecording();
    else
        stopRecording();
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
    const uint64_t tickForRow = (f.tick > 0) ? f.tick : ++phaseRowIndex;

    const glm::vec3 actualDelta = f.posAfter - f.posBefore;
    const glm::vec3 expectedDelta = f.velBefore * (1.0f / 128.0f); // assume 128 Hz; precise dt not exposed here
    const float actualMag =
        std::sqrt(actualDelta.x * actualDelta.x + actualDelta.y * actualDelta.y + actualDelta.z * actualDelta.z);
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
                 "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
                 "%s\n",
                 static_cast<unsigned long>(tickForRow),
                 entt::to_integral(f.entity),
                 static_cast<double>(f.posBefore.x),
                 static_cast<double>(f.posBefore.y),
                 static_cast<double>(f.posBefore.z),
                 static_cast<double>(f.posAfterDepen.x),
                 static_cast<double>(f.posAfterDepen.y),
                 static_cast<double>(f.posAfterDepen.z),
                 static_cast<double>(f.posAfter.x),
                 static_cast<double>(f.posAfter.y),
                 static_cast<double>(f.posAfter.z),
                 static_cast<double>(f.velBefore.x),
                 static_cast<double>(f.velBefore.y),
                 static_cast<double>(f.velBefore.z),
                 static_cast<double>(f.velAfter.x),
                 static_cast<double>(f.velAfter.y),
                 static_cast<double>(f.velAfter.z),
                 static_cast<double>(actualMag),
                 static_cast<double>(expectedMag),
                 static_cast<double>(f.depenPushDistance),
                 f.bumpHits,
                 moveModeName(f.moveMode),
                 f.wallrunSide,
                 f.jumpCount,
                 static_cast<double>(f.lastHitNormal.x),
                 static_cast<double>(f.lastHitNormal.y),
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
                 (flagBits & static_cast<uint32_t>(PhaseFlag::InvalidState)) ? 1 : 0,
                 note);
    std::fflush(logFile);
}

void recordMovementFrame(const MovementFrame& f) noexcept
{
    if (!enabledFlag.load(std::memory_order_relaxed))
        return;

    std::lock_guard<std::mutex> lk(movementLogMutex);
    openMovementLazily();
    if (movementLogFile == nullptr)
        return;

    ++movementRowIndex;

    PhaseFlag flags = f.flags;
    const bool finite = finiteVec3(f.posBefore) && finiteVec3(f.posAfter) && finiteVec3(f.velBefore) &&
                        finiteVec3(f.velAfter) && finiteVec3(f.frontNormal) && finiteVec3(f.frontPoint) &&
                        finiteVec3(f.ledgeNormal) && finiteVec3(f.ledgePoint) && finiteVec3(f.climbWallNormal) &&
                        finiteVec3(f.storedLedgeNormal) && finiteVec3(f.storedLedgePoint) &&
                        std::isfinite(f.groundDistance) && std::isfinite(f.yaw) && std::isfinite(f.pitch);
    if (!finite)
        flags |= PhaseFlag::InvalidState;

    char note[64] = {0};
    std::strncpy(note, f.note, sizeof(note) - 1);
    if (note[0] == 0 && !finite)
        std::strncpy(note, "invalid-movement-state", sizeof(note) - 1);

    const uint32_t flagBits = static_cast<uint32_t>(flags);
    std::fprintf(movementLogFile,
                 "%lu,%u,%s,%s,%d,%d,"
                 "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                 "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                 "%d,%d,%d,%d,%d,%d,%d,%.6f,%.6f,"
                 "%d,%d,%.3f,"
                 "%.6f,%.6f,%.6f,%.3f,%.3f,%.3f,"
                 "%.6f,%.6f,%.6f,%.3f,%.3f,%.3f,"
                 "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                 "%.3f,%.3f,%.3f,%.6f,%.6f,"
                 "0x%X,%d,%s\n",
                 static_cast<unsigned long>(movementRowIndex),
                 entt::to_integral(f.entity),
                 moveModeName(f.modeBefore),
                 moveModeName(f.modeAfter),
                 f.groundedBefore ? 1 : 0,
                 f.groundedAfter ? 1 : 0,
                 static_cast<double>(f.posBefore.x),
                 static_cast<double>(f.posBefore.y),
                 static_cast<double>(f.posBefore.z),
                 static_cast<double>(f.posAfter.x),
                 static_cast<double>(f.posAfter.y),
                 static_cast<double>(f.posAfter.z),
                 static_cast<double>(f.velBefore.x),
                 static_cast<double>(f.velBefore.y),
                 static_cast<double>(f.velBefore.z),
                 static_cast<double>(f.velAfter.x),
                 static_cast<double>(f.velAfter.y),
                 static_cast<double>(f.velAfter.z),
                 f.inputForward ? 1 : 0,
                 f.inputBack ? 1 : 0,
                 f.inputLeft ? 1 : 0,
                 f.inputRight ? 1 : 0,
                 f.inputJump ? 1 : 0,
                 f.inputCrouch ? 1 : 0,
                 f.inputGrapple ? 1 : 0,
                 static_cast<double>(f.yaw),
                 static_cast<double>(f.pitch),
                 f.wallFront ? 1 : 0,
                 f.ledgeDetected ? 1 : 0,
                 static_cast<double>(f.groundDistance),
                 static_cast<double>(f.frontNormal.x),
                 static_cast<double>(f.frontNormal.y),
                 static_cast<double>(f.frontNormal.z),
                 static_cast<double>(f.frontPoint.x),
                 static_cast<double>(f.frontPoint.y),
                 static_cast<double>(f.frontPoint.z),
                 static_cast<double>(f.ledgeNormal.x),
                 static_cast<double>(f.ledgeNormal.y),
                 static_cast<double>(f.ledgeNormal.z),
                 static_cast<double>(f.ledgePoint.x),
                 static_cast<double>(f.ledgePoint.y),
                 static_cast<double>(f.ledgePoint.z),
                 static_cast<double>(f.climbWallNormal.x),
                 static_cast<double>(f.climbWallNormal.y),
                 static_cast<double>(f.climbWallNormal.z),
                 static_cast<double>(f.storedLedgeNormal.x),
                 static_cast<double>(f.storedLedgeNormal.y),
                 static_cast<double>(f.storedLedgeNormal.z),
                 static_cast<double>(f.storedLedgePoint.x),
                 static_cast<double>(f.storedLedgePoint.y),
                 static_cast<double>(f.storedLedgePoint.z),
                 static_cast<double>(f.climbTimer),
                 static_cast<double>(f.ledgeHoldTimer),
                 flagBits,
                 (flagBits & static_cast<uint32_t>(PhaseFlag::InvalidState)) ? 1 : 0,
                 note);
    std::fflush(movementLogFile);
}

void recordKccTimingFrame(const KccTimingFrame& f) noexcept
{
    if (!enabledFlag.load(std::memory_order_relaxed))
        return;

    std::lock_guard<std::mutex> lk(kccTimingLogMutex);
    openKccTimingLazily();
    if (kccTimingLogFile == nullptr)
        return;

    ++kccTimingRowIndex;

    std::fprintf(kccTimingLogFile,
                 "%lu,%u,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%s\n",
                 static_cast<unsigned long>(kccTimingRowIndex),
                 entt::to_integral(f.entity),
                 static_cast<unsigned long>(f.elapsedUs),
                 f.substeps,
                 f.caIterations,
                 f.clearanceQueries,
                 f.sweepQueries,
                 f.sweepHits,
                 f.usedWalkCapsule ? 1 : 0,
                 f.caExhausted ? 1 : 0,
                 f.grounded ? 1 : 0,
                 moveModeName(f.moveMode));
    std::fflush(kccTimingLogFile);
}

void recordDepenContact(const DepenContact& c) noexcept
{
    if (!enabledFlag.load(std::memory_order_relaxed))
        return;

    std::lock_guard<std::mutex> lk(depenLogMutex);
    openDepenLazily();
    if (depenLogFile == nullptr)
        return;

    ++depenRowIndex;

    const float depthOverR = (c.minkowskiR > 1e-6f) ? (c.depth / c.minkowskiR) : 0.0f;

    std::fprintf(depenLogFile,
                 "%lu,%u,"
                 "%.3f,%.3f,%.3f,"
                 "%.4f,%.4f,%.4f,"
                 "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                 "%.4f,%.4f,%.4f,%.3f,"
                 "%s,0x%X,0x%X\n",
                 static_cast<unsigned long>(depenRowIndex),
                 c.triId,
                 static_cast<double>(c.playerPos.x),
                 static_cast<double>(c.playerPos.y),
                 static_cast<double>(c.playerPos.z),
                 static_cast<double>(c.faceNormal.x),
                 static_cast<double>(c.faceNormal.y),
                 static_cast<double>(c.faceNormal.z),
                 static_cast<double>(c.v0.x),
                 static_cast<double>(c.v0.y),
                 static_cast<double>(c.v0.z),
                 static_cast<double>(c.v1.x),
                 static_cast<double>(c.v1.y),
                 static_cast<double>(c.v1.z),
                 static_cast<double>(c.v2.x),
                 static_cast<double>(c.v2.y),
                 static_cast<double>(c.v2.z),
                 static_cast<double>(c.signedDist),
                 static_cast<double>(c.minkowskiR),
                 static_cast<double>(c.depth),
                 static_cast<double>(depthOverR),
                 triRegionName(c.region),
                 static_cast<unsigned>(c.edgeFlags),
                 static_cast<unsigned>(c.vertFlags));
    std::fflush(depenLogFile);
}

} // namespace physics::diag
