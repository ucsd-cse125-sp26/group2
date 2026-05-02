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
    std::fprintf(file, "wallTimeNs,shooterClientId,shotInputTick,hitClientId,hitX,hitY,hitZ,hitRegion\n");
    std::fflush(file);
    SDL_Log("[server] PR-18b: writing shot-resolution log to %s", path);
}

void recordShotResolution(std::uint16_t shooterClientId,
                          std::uint32_t shotInputTick,
                          std::uint16_t hitClientId,
                          float hitX,
                          float hitY,
                          float hitZ,
                          int hitRegion)
{
    std::lock_guard<std::mutex> lock(mu);
    if (file == nullptr)
        return;

    const Uint64 nowNs = SDL_GetTicksNS();
    std::fprintf(file,
                 "%llu,%u,%u,%u,%.4f,%.4f,%.4f,%d\n",
                 static_cast<unsigned long long>(nowNs),
                 static_cast<unsigned>(shooterClientId),
                 static_cast<unsigned>(shotInputTick),
                 static_cast<unsigned>(hitClientId),
                 static_cast<double>(hitX),
                 static_cast<double>(hitY),
                 static_cast<double>(hitZ),
                 hitRegion);
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
