/// @file Profiler.hpp
/// @brief Lightweight server-side scoped profiler.
///
/// Goal: measure where the per-tick budget goes at 150–500 connected
/// bots without perturbing the measurement. Cost per scope is ~70 ns
/// (two `SDL_GetPerformanceCounter` reads + a handful of relaxed
/// atomic updates). At ~14 scopes per tick × 128 Hz that is ~125 µs/s
/// of pure profiler overhead — a measurement noise floor we treat as
/// the empty cost of having the system on at all.
///
/// Layered on top:
///   - `GROUP2_PROF_SCOPE("name")`: drop-in scoped timer.
///   - `Profiler::tickEnd()`: call once per server tick from the
///     game thread; updates the per-tick wall-clock counter that
///     drives the 1 Hz aggregator.
///   - `Profiler::startAggregator(loggerCallback)`: spawns a worker
///     thread that wakes once per second, snapshots and zeros the
///     scope stats + network counters, and invokes the callback
///     with a structured `Snapshot`. Callers route the snapshot to
///     SDL_Log + an optional CSV file.
///
/// Toggling: `GROUP2_SERVER_PROFILE=1` enables sample collection at
/// startup. With the env var unset the macro still expands but every
/// `ScopeTimer` ctor early-outs on the cached `enabled.load()`,
/// so the cost collapses to one relaxed atomic load per scope (~1 ns).
///
/// Thread-safety: every counter in this header is `std::atomic` with
/// relaxed ordering. We deliberately do not use a per-thread buffer
/// flushed at aggregation time — the simpler all-atomic path is
/// correct under any future parallel-system layout (PR-3+) and the
/// extra atomic-ops cost is below the SDL clock-read cost we pay
/// anyway.

#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>

namespace group2::perf
{

/// Compile-time caps. Both fit in a single CPU cache line per scope
/// (PerScopeStats is ~512 B; we keep it small enough for hot scopes
/// to coexist in L2).
inline constexpr std::size_t k_maxScopes = 64;

/// Histogram bucket count. Buckets are log2-spaced over the SDL
/// performance-counter tick scale (resolution typically 100 ns).
/// Index = `__builtin_clzll`-derived msb position; bucket 0 holds
/// the smallest measurable values.
inline constexpr std::size_t k_histogramBuckets = 32;

/// Dense small id used to index the global stats table.
using ScopeId = std::uint16_t;
inline constexpr ScopeId k_invalidScope = static_cast<ScopeId>(-1);

/// Register (or look up) a scope name and return its dense id.
/// First call for a given name is `O(n)` over already-registered
/// scopes; subsequent calls are cached at the call site.
/// Thread-safe.
ScopeId registerScope(const char* name);

/// Returns the human-readable name a `ScopeId` was registered with,
/// or "" if `id` is out of range. Used by the aggregator's logger.
const char* scopeName(ScopeId id);

/// Returns the highest registered id + 1.
std::size_t scopeCount();

/// Per-scope, all-thread atomic counters.
struct PerScopeStats
{
    std::atomic<std::uint64_t> count{0};
    std::atomic<std::uint64_t> sumTicks{0};
    std::atomic<std::uint64_t> minTicks{std::numeric_limits<std::uint64_t>::max()};
    std::atomic<std::uint64_t> maxTicks{0};
    std::array<std::atomic<std::uint32_t>, k_histogramBuckets> hist{};
};

/// Per-tick network counters maintained by the network code.
/// Aggregated and zeroed once per second alongside scope stats.
struct NetworkCounters
{
    std::atomic<std::uint64_t> bytesSent{0};
    std::atomic<std::uint64_t> bytesRecv{0};
    std::atomic<std::uint64_t> snapshotsSent{0};
    std::atomic<std::uint64_t> packetsSent{0};
    std::atomic<std::uint64_t> packetsRecv{0};
    std::atomic<std::uint32_t> peakBacklog{0};
    std::atomic<std::uint32_t> clientCount{0};
};

/// Globally-visible snapshot returned to the aggregator callback.
struct Snapshot
{
    /// Per-scope summary, filled for `[0, scopeCount)`.
    struct ScopeSummary
    {
        const char* name = "";
        std::uint64_t count = 0;
        std::uint64_t minNs = 0;
        std::uint64_t p50Ns = 0;
        std::uint64_t p99Ns = 0;
        std::uint64_t maxNs = 0;
        std::uint64_t meanNs = 0;
    };

    std::array<ScopeSummary, k_maxScopes> scopes{};
    std::size_t scopeNum = 0;

    /// Per-tick wall time over the aggregation window.
    std::uint64_t tickCount = 0;
    std::uint64_t tickSumNs = 0;
    std::uint64_t tickMaxNs = 0;
    std::uint64_t tickP99Ns = 0;

    /// Network totals over the window.
    std::uint64_t bytesSent = 0;
    std::uint64_t bytesRecv = 0;
    std::uint64_t snapshotsSent = 0;
    std::uint64_t packetsSent = 0;
    std::uint64_t packetsRecv = 0;
    std::uint32_t peakBacklog = 0;
    std::uint32_t clientCount = 0;

    /// Wall-clock window covered (POSIX millis since epoch).
    std::uint64_t windowStartMs = 0;
    std::uint64_t windowEndMs = 0;
};

/// Master switch. Toggled at process startup based on
/// `GROUP2_SERVER_PROFILE`. When false, `ScopeTimer` ctor early-outs
/// after a single relaxed atomic load.
extern std::atomic<bool> enabled;

/// Initialize from environment variables. Call once at startup,
/// before the first scope is hit.
///   GROUP2_SERVER_PROFILE=1   → enable sampling + 1 Hz log line
///   GROUP2_SERVER_PROFILE_CSV=path → also write CSV rows to `path`
///
/// Idempotent.
void initFromEnv();

/// Spawn the 1 Hz aggregator thread. Calls `cb(snap)` once per
/// second on a dedicated thread. Safe to call once. `cb` runs on
/// the aggregator thread, so do not touch ECS / non-thread-safe
/// state from inside it.
void startAggregator(std::function<void(const Snapshot&)> cb);

/// Stop the aggregator and join its thread. Idempotent.
void stopAggregator();

/// Recording entry point — public so unit tests can invoke it
/// directly without a real `ScopeTimer`.
void recordSample(ScopeId id, std::uint64_t ticks) noexcept;

/// Tick boundary marker — call once per server `tick()` end.
void tickEnd(std::uint64_t tickWallNs) noexcept;

/// Network counter accessor. Hot-path code increments these directly.
inline NetworkCounters& net()
{
    static NetworkCounters c;
    return c;
}

/// Convenience: convert SDL performance-counter ticks to nanoseconds.
inline std::uint64_t ticksToNs(std::uint64_t ticks) noexcept
{
    static const std::uint64_t k_freq = SDL_GetPerformanceFrequency();
    // `ticks * 1e9 / freq` with intermediate clamp to avoid overflow at
    // ~18.4 s on a 1 GHz counter. We never measure that long.
    return (ticks / k_freq) * 1'000'000'000ULL + ((ticks % k_freq) * 1'000'000'000ULL) / k_freq;
}

/// RAII scoped timer. ctor reads enabled flag + start counter; dtor
/// records the delta if armed.
class ScopeTimer
{
public:
    explicit ScopeTimer(ScopeId id) noexcept : scopeId(id), recording(enabled.load(std::memory_order_relaxed))
    {
        if (recording)
            startCounter = SDL_GetPerformanceCounter();
    }
    ~ScopeTimer() noexcept
    {
        if (!recording)
            return;
        const std::uint64_t end = SDL_GetPerformanceCounter();
        recordSample(scopeId, end - startCounter);
    }

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
    ScopeTimer(ScopeTimer&&) = delete;
    ScopeTimer& operator=(ScopeTimer&&) = delete;

private:
    ScopeId scopeId{k_invalidScope};
    bool recording{false};
    std::uint64_t startCounter{0};
};

} // namespace group2::perf

// ────────────────────────────────────────────────────────────────────
// Macros
// ────────────────────────────────────────────────────────────────────

/// @cond — paste helpers
#define GROUP2_PROF_CAT2(a, b) a##b
#define GROUP2_PROF_CAT(a, b) GROUP2_PROF_CAT2(a, b)
/// @endcond

/// Drop-in scoped timer. The scope name is registered exactly once
/// per call site (cached via a function-local `static const`).
///
/// Note: we use `__LINE__` rather than `__COUNTER__` because the
/// latter is a compiler extension flagged by `-Wpedantic` /
/// `-Wc2y-extensions`. This means two `GROUP2_PROF_SCOPE` calls on
/// the same source line would collide; in practice every scope
/// macro lives on its own line inside a brace block, so the
/// collision can't happen.
///
/// Usage:
/// @code
/// void tick() {
///     GROUP2_PROF_SCOPE("tick");
///     // ... work ...
/// }
/// @endcode
#define GROUP2_PROF_SCOPE(label)                                                                                       \
    ::group2::perf::ScopeTimer GROUP2_PROF_CAT(_gpScope, __LINE__)                                                     \
    {                                                                                                                  \
        []() noexcept -> ::group2::perf::ScopeId {                                                                     \
            static const ::group2::perf::ScopeId k_id = ::group2::perf::registerScope(label);                          \
            return k_id;                                                                                               \
        }()                                                                                                            \
    }
