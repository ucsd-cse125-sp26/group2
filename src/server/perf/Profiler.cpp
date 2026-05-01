/// @file Profiler.cpp
/// @brief Profiler runtime: scope registration, sample recording,
///        1 Hz aggregator thread.

#include "Profiler.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace group2::perf
{

namespace
{

// ── Scope registry ─────────────────────────────────────────────────
//
// Lookup-then-insert under a single mutex. Hit by every macro the
// first time a call site fires; subsequent uses skip this entirely
// thanks to the `static const` cached at the call site.
//
// We can't use a hash map without dragging in unordered_map and
// growing on the hot path; the registry is bounded by `k_maxScopes`
// and the linear scan stays in L1.

std::mutex registryMutex;
std::array<const char*, k_maxScopes> scopeNames{};
std::atomic<std::size_t> scopeNumAtomic{0};

// ── Per-scope stats table ──────────────────────────────────────────
//
// Heap-allocated to keep the .bss section small and allow ASan to see
// out-of-bounds writes. Constructed on first use.

/// Per-scope stats. Direct static storage instead of a heap pointer:
/// keeps the lookup branch-free and dodges the "unhandled bad_alloc"
/// diagnostic the heap variant attracts.
PerScopeStats& statsFor(ScopeId id)
{
    static std::array<PerScopeStats, k_maxScopes> table;
    return table[id];
}

// ── Tick wall-clock counters (single-thread producer = game thread) ─

std::atomic<std::uint64_t> tickCount{0};
std::atomic<std::uint64_t> tickSumNs{0};
std::atomic<std::uint64_t> tickMaxNs{0};
std::array<std::atomic<std::uint32_t>, k_histogramBuckets> tickHist{};

// ── Aggregator state ───────────────────────────────────────────────

std::atomic<bool> aggregatorRunning{false};
std::thread aggregatorThread;

// ── Histogram helpers ──────────────────────────────────────────────

/// Map a tick count (or ns) to a log2 bucket. Bucket `b` covers
/// `[2^b, 2^(b+1))` ticks.
inline std::size_t bucketOf(std::uint64_t v) noexcept
{
    if (v < 2)
        return 0;
    // 63 - leading-zeros gives msb position; clamp to range.
#if defined(__GNUC__) || defined(__clang__)
    const auto b = 63u - static_cast<unsigned>(__builtin_clzll(v));
#else
    unsigned b = 0;
    while ((v >>= 1u) != 0u)
        ++b;
#endif
    return std::min<std::size_t>(b, k_histogramBuckets - 1);
}

/// Histogram → percentile estimate. `target` is `count * pct` rounded
/// down. Returns the midpoint of the first bucket whose cumulative
/// count crosses `target`. Worst-case relative error is 2× (one
/// log2 bucket); fine for monitoring.
std::uint64_t percentileTicksFromHist(const std::array<std::atomic<std::uint32_t>, k_histogramBuckets>& hist,
                                      std::uint64_t totalCount,
                                      double pct)
{
    if (totalCount == 0)
        return 0;
    const auto target = static_cast<std::uint64_t>(static_cast<double>(totalCount) * pct);
    std::uint64_t cum = 0;
    for (std::size_t i = 0; i < k_histogramBuckets; ++i) {
        cum += hist[i].load(std::memory_order_relaxed);
        if (cum >= target) {
            // Bucket i covers [2^i, 2^(i+1)); midpoint ≈ 2^i + 2^(i-1) = 3*2^(i-1)
            return (i == 0) ? 1ULL : (3ULL << (i - 1));
        }
    }
    return (1ULL << (k_histogramBuckets - 1));
}

void resetScopeStats(PerScopeStats& s)
{
    s.count.store(0, std::memory_order_relaxed);
    s.sumTicks.store(0, std::memory_order_relaxed);
    s.minTicks.store(std::numeric_limits<std::uint64_t>::max(), std::memory_order_relaxed);
    s.maxTicks.store(0, std::memory_order_relaxed);
    for (auto& b : s.hist)
        b.store(0, std::memory_order_relaxed);
}

void resetTickStats()
{
    tickCount.store(0, std::memory_order_relaxed);
    tickSumNs.store(0, std::memory_order_relaxed);
    tickMaxNs.store(0, std::memory_order_relaxed);
    for (auto& b : tickHist)
        b.store(0, std::memory_order_relaxed);
}

void resetNetCounters(NetworkCounters& c)
{
    c.bytesSent.store(0, std::memory_order_relaxed);
    c.bytesRecv.store(0, std::memory_order_relaxed);
    c.snapshotsSent.store(0, std::memory_order_relaxed);
    c.packetsSent.store(0, std::memory_order_relaxed);
    c.packetsRecv.store(0, std::memory_order_relaxed);
    c.peakBacklog.store(0, std::memory_order_relaxed);
    // Note: clientCount is a gauge — don't zero it; the network
    // thread keeps it current.
}

Snapshot snapshot()
{
    Snapshot snap{};
    snap.scopeNum = scopeNumAtomic.load(std::memory_order_acquire);

    for (std::size_t i = 0; i < snap.scopeNum; ++i) {
        auto& s = statsFor(static_cast<ScopeId>(i));
        const std::uint64_t cnt = s.count.load(std::memory_order_relaxed);
        const std::uint64_t sum = s.sumTicks.load(std::memory_order_relaxed);
        const std::uint64_t mn = s.minTicks.load(std::memory_order_relaxed);
        const std::uint64_t mx = s.maxTicks.load(std::memory_order_relaxed);

        auto& summary = snap.scopes[i];
        summary.name = scopeName(static_cast<ScopeId>(i));
        summary.count = cnt;
        summary.minNs = (cnt == 0) ? 0 : ticksToNs(mn);
        summary.maxNs = ticksToNs(mx);
        summary.meanNs = (cnt == 0) ? 0 : ticksToNs(sum / cnt);
        summary.p50Ns = ticksToNs(percentileTicksFromHist(s.hist, cnt, 0.50));
        summary.p99Ns = ticksToNs(percentileTicksFromHist(s.hist, cnt, 0.99));

        resetScopeStats(s);
    }

    snap.tickCount = tickCount.load(std::memory_order_relaxed);
    snap.tickSumNs = tickSumNs.load(std::memory_order_relaxed);
    snap.tickMaxNs = tickMaxNs.load(std::memory_order_relaxed);
    snap.tickP99Ns = percentileTicksFromHist(tickHist, snap.tickCount, 0.99);
    resetTickStats();

    auto& nc = net();
    snap.bytesSent = nc.bytesSent.load(std::memory_order_relaxed);
    snap.bytesRecv = nc.bytesRecv.load(std::memory_order_relaxed);
    snap.snapshotsSent = nc.snapshotsSent.load(std::memory_order_relaxed);
    snap.packetsSent = nc.packetsSent.load(std::memory_order_relaxed);
    snap.packetsRecv = nc.packetsRecv.load(std::memory_order_relaxed);
    snap.peakBacklog = nc.peakBacklog.load(std::memory_order_relaxed);
    snap.clientCount = nc.clientCount.load(std::memory_order_relaxed);
    resetNetCounters(nc);

    using namespace std::chrono;
    snap.windowEndMs =
        static_cast<std::uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    snap.windowStartMs = (snap.windowEndMs >= 1000) ? snap.windowEndMs - 1000 : 0;

    return snap;
}

} // namespace

// ── Public API ─────────────────────────────────────────────────────

std::atomic<bool> enabled{false};

ScopeId registerScope(const char* name)
{
    std::lock_guard<std::mutex> lock(registryMutex);

    const std::size_t n = scopeNumAtomic.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < n; ++i) {
        if (scopeNames[i] != nullptr && std::strcmp(scopeNames[i], name) == 0)
            return static_cast<ScopeId>(i);
    }

    if (n >= k_maxScopes) {
        SDL_Log("[perf] WARNING: scope table full at %zu entries; '%s' not registered", n, name);
        return k_invalidScope;
    }

    scopeNames[n] = name;
    scopeNumAtomic.store(n + 1, std::memory_order_release);
    return static_cast<ScopeId>(n);
}

const char* scopeName(ScopeId id)
{
    if (id == k_invalidScope || id >= k_maxScopes)
        return "";
    const char* p = scopeNames[id];
    return p ? p : "";
}

std::size_t scopeCount()
{
    return scopeNumAtomic.load(std::memory_order_acquire);
}

void recordSample(ScopeId id, std::uint64_t ticks) noexcept
{
    if (id == k_invalidScope || id >= k_maxScopes)
        return;
    auto& s = statsFor(id);
    s.count.fetch_add(1, std::memory_order_relaxed);
    s.sumTicks.fetch_add(ticks, std::memory_order_relaxed);

    // Atomic min/max via cmpxchg loops. Both converge in 1–2 iters.
    std::uint64_t cur = s.minTicks.load(std::memory_order_relaxed);
    while (ticks < cur && !s.minTicks.compare_exchange_weak(cur, ticks, std::memory_order_relaxed)) {
        // cur was reloaded by the failed exchange
    }
    cur = s.maxTicks.load(std::memory_order_relaxed);
    while (ticks > cur && !s.maxTicks.compare_exchange_weak(cur, ticks, std::memory_order_relaxed)) {
    }

    s.hist[bucketOf(ticks)].fetch_add(1, std::memory_order_relaxed);
}

void tickEnd(std::uint64_t tickWallNs) noexcept
{
    if (!enabled.load(std::memory_order_relaxed))
        return;
    tickCount.fetch_add(1, std::memory_order_relaxed);
    tickSumNs.fetch_add(tickWallNs, std::memory_order_relaxed);

    std::uint64_t cur = tickMaxNs.load(std::memory_order_relaxed);
    while (tickWallNs > cur && !tickMaxNs.compare_exchange_weak(cur, tickWallNs, std::memory_order_relaxed)) {
    }
    tickHist[bucketOf(tickWallNs)].fetch_add(1, std::memory_order_relaxed);
}

void initFromEnv()
{
    const char* p = std::getenv("GROUP2_SERVER_PROFILE");
    const bool wantOn = (p != nullptr) && p[0] != '\0' && p[0] != '0';
    enabled.store(wantOn, std::memory_order_release);
    SDL_Log("[perf] sampling %s (set GROUP2_SERVER_PROFILE=1 to enable)", wantOn ? "ENABLED" : "disabled");
}

void startAggregator(std::function<void(const Snapshot&)> cb)
{
    if (aggregatorRunning.exchange(true)) {
        SDL_Log("[perf] aggregator already running");
        return;
    }

    aggregatorThread = std::thread([cb = std::move(cb)]() {
        // Period: 1 second. We sleep first so PR-1's "first second"
        // includes startup work without claiming a steady-state number.
        while (aggregatorRunning.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!aggregatorRunning.load(std::memory_order_relaxed))
                break;
            if (!enabled.load(std::memory_order_relaxed))
                continue;
            const Snapshot snap = snapshot();
            if (cb)
                cb(snap);
        }
    });
}

void stopAggregator()
{
    if (!aggregatorRunning.exchange(false))
        return;
    if (aggregatorThread.joinable())
        aggregatorThread.join();
}

} // namespace group2::perf
