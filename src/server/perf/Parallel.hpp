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
/// Defaults ON as of PR-8. Earlier benches (idle-bot loadtest,
/// pre-PR-7) suggested defaulting off because the synthetic test's
/// per-item work was too small. With AI bots actually moving + PR-7
/// (collision/movement parallel) + PR-8 (per-component-type
/// parallel serialization) the per-item work is meaningful and the
/// 16-core box pays clear dividends:
///
///   N=100, AI:  tick p99 1.57 ms (off) → 0.39 ms (on)
///   N=300, AI:  tick p99 12 ms   (off) → 1.57 ms (on)
///   N=500, AI:  tick p99 50+ ms  (off) → 3.15 ms when OS gives CPU
///
/// Below the `k_parallelThreshold` element-count, parallelFor
/// short-circuits to sequential anyway, so small inputs still win.
///
/// Kill switch: `GROUP2_SERVER_PARALLEL=0` flips back to sequential
/// without rebuilding — useful for diff bisection if a regression
/// appears.
inline std::atomic<bool> parallelEnabled{true};

/// Minimum items below which `parallelFor` runs sequentially even
/// when the master switch is on. Avoids paying TBB dispatch overhead
/// for trivially-small work where sequential is faster.
inline constexpr std::size_t k_parallelThreshold = 64;

/// Initialize from environment. Idempotent.
///
/// Default ON (PR-8). `GROUP2_SERVER_PARALLEL=0` flips it off for
/// diagnostics / A-B comparison; any other value (or unset) leaves
/// it on.
inline void initParallelFromEnv()
{
    const char* p = std::getenv("GROUP2_SERVER_PARALLEL");
    const bool wantOff = p != nullptr && (p[0] == '0' || p[0] == 'f' || p[0] == 'F' || p[0] == 'n' || p[0] == 'N');
    const bool wantOn = !wantOff;
    parallelEnabled.store(wantOn, std::memory_order_release);
#if defined(GROUP2_HAVE_TBB)
    SDL_Log("[perf] parallel kernels: %s (TBB-backed; default ON, set GROUP2_SERVER_PARALLEL=0 to disable)",
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
