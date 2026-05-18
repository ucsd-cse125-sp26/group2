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
///     we use a small persistent `std::thread` pool. CMake defines
///     `GROUP2_HAVE_TBB` when the parallel-STL path is wired.
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
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <utility>

#if defined(GROUP2_HAVE_TBB)
#include <execution>
#else
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
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
inline constexpr std::size_t k_parallelThreshold = 8;

#if !defined(GROUP2_HAVE_TBB)

inline thread_local bool inParallelKernel = false;

class ParallelFlagGuard
{
public:
    ParallelFlagGuard() : previous_(inParallelKernel) { inParallelKernel = true; }
    ~ParallelFlagGuard() { inParallelKernel = previous_; }

private:
    bool previous_;
};

inline unsigned fallbackWorkerCountFromEnv()
{
    const unsigned hw = std::thread::hardware_concurrency();
    unsigned count = (hw > 1) ? (hw - 1) : 0;

    const char* p = std::getenv("GROUP2_SERVER_THREADS");
    if (p == nullptr || p[0] == '\0')
        return count;

    char* end = nullptr;
    const unsigned long requested = std::strtoul(p, &end, 10);
    if (end == p)
        return count;

    constexpr unsigned k_maxFallbackWorkers = 64;
    return static_cast<unsigned>(std::min<unsigned long>(requested, k_maxFallbackWorkers));
}

class FallbackThreadPool
{
public:
    FallbackThreadPool()
    {
        const unsigned count = fallbackWorkerCountFromEnv();
        workers_.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~FallbackThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable())
                worker.join();
        }
    }

    FallbackThreadPool(const FallbackThreadPool&) = delete;
    FallbackThreadPool& operator=(const FallbackThreadPool&) = delete;

    std::size_t workerCount() const noexcept { return workers_.size(); }

    template <class Job>
    void enqueue(Job&& job)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.emplace_back(std::forward<Job>(job));
        }
        cv_.notify_one();
    }

private:
    void workerLoop()
    {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (stopping_ && jobs_.empty())
                    return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            job();
        }
    }

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> jobs_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

inline FallbackThreadPool& fallbackPool()
{
    static FallbackThreadPool pool;
    return pool;
}

#endif

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
    if (wantOn) {
        const std::size_t workers = fallbackPool().workerCount();
        SDL_Log(
            "[perf] parallel kernels: %s (std::thread fallback, %zu workers; set GROUP2_SERVER_PARALLEL=0 to disable)",
            workers > 0 ? "ENABLED" : "sequential",
            workers);
    } else {
        SDL_Log("[perf] parallel kernels: disabled (std::thread fallback available)");
    }
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
#else
    const auto distance = std::distance(begin, end);
    if (distance <= 0)
        return;

    const auto count = static_cast<std::size_t>(distance);
    if (parallelEnabled.load(std::memory_order_relaxed) && count >= k_parallelThreshold && !inParallelKernel) {
        FallbackThreadPool& pool = fallbackPool();
        const std::size_t workers = pool.workerCount();
        if (workers > 0) {
            const std::size_t chunks = std::min<std::size_t>(count, workers + 1);
            if (chunks > 1) {
                struct WaitState
                {
                    std::mutex mutex;
                    std::condition_variable cv;
                    std::size_t remaining = 0;
                    std::exception_ptr exception;
                };

                auto state = std::make_shared<WaitState>();
                state->remaining = chunks - 1;

                auto runChunk = [&](std::size_t chunk) {
                    const std::size_t first = (chunk * count) / chunks;
                    const std::size_t last = ((chunk + 1) * count) / chunks;
                    auto it = begin;
                    std::advance(it, static_cast<decltype(distance)>(first));
                    auto chunkEnd = begin;
                    std::advance(chunkEnd, static_cast<decltype(distance)>(last));
                    for (; it != chunkEnd; ++it) {
                        fn(*it);
                    }
                };

                auto finish = [state](std::exception_ptr ex) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (ex && !state->exception)
                        state->exception = ex;
                    if (--state->remaining == 0)
                        state->cv.notify_one();
                };

                for (std::size_t chunk = 1; chunk < chunks; ++chunk) {
                    pool.enqueue([&, finish, chunk] {
                        std::exception_ptr ex;
                        try {
                            ParallelFlagGuard guard;
                            runChunk(chunk);
                        } catch (...) {
                            ex = std::current_exception();
                        }
                        finish(ex);
                    });
                }

                std::exception_ptr mainException;
                try {
                    ParallelFlagGuard guard;
                    runChunk(0);
                } catch (...) {
                    mainException = std::current_exception();
                }

                {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    state->cv.wait(lock, [state] { return state->remaining == 0; });
                }

                if (mainException)
                    std::rethrow_exception(mainException);
                if (state->exception)
                    std::rethrow_exception(state->exception);
                return;
            }
        }
    }
#endif
    std::for_each(begin, end, std::forward<Fn>(fn));
}

} // namespace group2::perf
