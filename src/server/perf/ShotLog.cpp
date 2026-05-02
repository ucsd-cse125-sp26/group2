/// @file ShotLog.cpp
/// @brief Implementation of the server-side shot-resolution log.

#include "ShotLog.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace group2::perf::shotlog
{

namespace
{

// File-scope state — single instance per server process.  Guarded by
// `mu` so the three WeaponSystem fire sites (beam, discrete, charge)
// can write concurrently from the game thread without tearing rows.
// In practice the game thread is single-threaded today (PR-7
// parallelizes only ECS systems that don't fire shots), so the
// mutex is uncontended; it's there for safety and so future
// multi-threaded weapon work doesn't introduce a hidden race.
std::mutex mu;
std::FILE* file = nullptr;
bool initTried = false;

} // namespace

void openIfRequested()
{
    std::lock_guard<std::mutex> lock(mu);
    if (initTried)
        return;
    initTried = true;

    const char* path = std::getenv("GROUP2_SERVER_SHOTS_CSV");
    if (path == nullptr || path[0] == '\0')
        return;

    file = std::fopen(path, "w");
    if (file == nullptr) {
        SDL_Log("[server] PR-18b: failed to open shots log at %s", path);
        return;
    }
    // PR-22: schema grew from 8 → 23 columns.  Old runs without the
    // PR-22 columns are still loadable by the analyzer because we use
    // a DictReader and tolerate missing keys.
    std::fprintf(file,
                 "wallTimeNs,shooterClientId,shotInputTick,"
                 "hitClientId,hitX,hitY,hitZ,hitRegion,"
                 "originX,originY,originZ,"
                 "dirX,dirY,dirZ,"
                 "shooterRttMs,lagCompTicks,"
                 "hitTargetRewoundX,hitTargetRewoundY,hitTargetRewoundZ,"
                 "hitTargetCurrentX,hitTargetCurrentY,hitTargetCurrentZ\n");
    std::fflush(file);
    SDL_Log("[server] PR-18b: writing shot-resolution log to %s", path);
}

void recordShotResolution(const ShotResolution& shot)
{
    std::lock_guard<std::mutex> lock(mu);
    if (file == nullptr)
        return;

    const Uint64 nowNs = SDL_GetTicksNS();
    std::fprintf(file,
                 "%llu,%u,%u,%u,%.4f,%.4f,%.4f,%d,"
                 "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
                 "%u,%u,"
                 "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                 static_cast<unsigned long long>(nowNs),
                 static_cast<unsigned>(shot.shooterClientId),
                 static_cast<unsigned>(shot.shotInputTick),
                 static_cast<unsigned>(shot.hitClientId),
                 static_cast<double>(shot.hitX),
                 static_cast<double>(shot.hitY),
                 static_cast<double>(shot.hitZ),
                 shot.hitRegion,
                 static_cast<double>(shot.originX),
                 static_cast<double>(shot.originY),
                 static_cast<double>(shot.originZ),
                 static_cast<double>(shot.dirX),
                 static_cast<double>(shot.dirY),
                 static_cast<double>(shot.dirZ),
                 static_cast<unsigned>(shot.shooterRttMs),
                 static_cast<unsigned>(shot.lagCompTicks),
                 static_cast<double>(shot.hitTargetRewoundX),
                 static_cast<double>(shot.hitTargetRewoundY),
                 static_cast<double>(shot.hitTargetRewoundZ),
                 static_cast<double>(shot.hitTargetCurrentX),
                 static_cast<double>(shot.hitTargetCurrentY),
                 static_cast<double>(shot.hitTargetCurrentZ));
    // No flush — we'd be calling it ~150-300 times/sec at high
    // fire-rate fleet sizes.  The game thread's tick-end profiler
    // flush gives a natural every-1s snapshot anyway, and the file
    // gets flushed on close().
}

void close() noexcept
{
    std::lock_guard<std::mutex> lock(mu);
    if (file != nullptr) {
        std::fclose(file);
        file = nullptr;
    }
    initTried = false;
}

} // namespace group2::perf::shotlog
