/// @file Parallel.hpp
/// @brief Thin wrapper around C++17 parallel STL.
///
/// PR-3 (server-perf): the per-system game-thread work is largely
/// embarrassingly parallel across players (animation update, hitbox
/// capsule transform, swept collision, etc.). This header gives the
/// systems a single, opinionated entry point —
/// `group2::perf::parallelFor(begin, end, fn)` — that hides the
/// libstdc++ parallel-STL details.
///
/// Implementation policy:
///   - On Linux with TBB installed (the common dev / CI path), we
///     route through `std::for_each(std::execution::par_unseq, ...)`,
///     which libstdc++ implements via TBB's task-group scheduler.
///   - On platforms without TBB (currently a CMake fallback path:
///     macOS Homebrew without explicit install, some Linux distros),
///     we fall back to a sequential `std::for_each` so behaviour stays
///     correct and the build still links. CMake defines
///     `GROUP2_HAVE_TBB` when the parallel path is wired.
///
/// Determinism: lag-comp + hitscan rely on the simulation being
/// deterministic across the network/recording boundary. par_unseq
/// reorders operations across iterations BUT operates only on
/// independent per-entity slots, so the observable result is
/// identical to a sequential pass. We add a `GROUP2_SERVER_PARALLEL=0`
/// kill-switch (read once at startup) to fall back to sequential
/// if a regression turns up — the brain wants the diff to be exactly
/// "thread count" so repro is easy.

#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>

#if defined(GROUP2_HAVE_TBB)
#include <execution>
#endif

namespace group2::perf
{

/// Master switch for parallel execution.
///
/// Defaults OFF. Reasoning: empirical results on the localhost
/// loadtest harness show parallel-STL hurts at moderate N (≤200)
/// because the per-item work is small (tens of microseconds) and
/// the TBB worker pool oversubscribes the cores already shared with
/// the clientbot fleet, producing cache thrash and bot-thread
/// preemption. The win shows up when (a) the server has dedicated
/// cores (deployment, not localhost loadtest) and (b) N is large
/// enough that work_per_item × N / threads >> dispatch_overhead.
///
/// Set `GROUP2_SERVER_PARALLEL=1` to enable; the default sequential
/// path matches PR-2c's measured behaviour for backwards-compat.
inline std::atomic<bool> parallelEnabled{false};

/// Minimum items below which `parallelFor` runs sequentially even
/// when the master switch is on. Avoids paying TBB dispatch overhead
/// for trivially-small work where sequential is faster.
inline constexpr std::size_t k_parallelThreshold = 64;

/// Initialize from environment. Idempotent.
inline void initParallelFromEnv()
{
    const char* p = std::getenv("GROUP2_SERVER_PARALLEL");
    // Off by default; opt in via "1", "true", "yes", "on".
    const bool wantOn = p != nullptr && (p[0] == '1' || p[0] == 't' || p[0] == 'T' || p[0] == 'y' || p[0] == 'Y' ||
                                         p[0] == 'o' || p[0] == 'O');
    parallelEnabled.store(wantOn, std::memory_order_release);
#if defined(GROUP2_HAVE_TBB)
    SDL_Log("[perf] parallel kernels: %s (TBB-backed; opt in via GROUP2_SERVER_PARALLEL=1)",
            wantOn ? "ENABLED" : "disabled");
#else
    SDL_Log("[perf] parallel kernels: sequential fallback (TBB not linked)");
#endif
}

/// Call `fn(*it)` for every element in `[begin, end)`. Routes
/// through TBB when (a) available, (b) the runtime flag is on, and
/// (c) the input range is large enough to amortize dispatch cost.
/// Otherwise sequential.
template <class Iter, class Fn>
inline void parallelFor(Iter begin, Iter end, Fn&& fn)
{
#if defined(GROUP2_HAVE_TBB)
    const auto distance = std::distance(begin, end);
    if (parallelEnabled.load(std::memory_order_relaxed) && static_cast<std::size_t>(distance) >= k_parallelThreshold) {
        std::for_each(std::execution::par_unseq, begin, end, std::forward<Fn>(fn));
        return;
    }
#endif
    std::for_each(begin, end, std::forward<Fn>(fn));
}

} // namespace group2::perf
