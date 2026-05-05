/// @file WorkerPool.hpp
/// @brief Tiny persistent thread pool for parallel-for over short job ranges.
///
/// Designed for the per-frame "split this loop across N workers, wait for
/// all to finish" pattern.  Workers spin on a condition variable between
/// jobs (no thread create / destroy churn), so the dispatch overhead is
/// ~5–10 µs round-trip — small enough to be useful at 1000+ Hz frame rates
/// for ranges that take more than a few µs of actual work.
///
/// Not a general-purpose scheduler: one job at a time, no priorities, no
/// dependencies.  Use it for parallel animation sampling, frustum culling,
/// SoA component transforms, etc.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class WorkerPool
{
public:
    /// @brief Spin up @p numWorkers persistent threads.
    /// @param numWorkers  Worker count.  Caller picks; a sane default is
    ///                    `std::thread::hardware_concurrency() / 2` so we
    ///                    leave half the cores for the rest of the system
    ///                    (server + bots in our test scenario, OS in prod).
    explicit WorkerPool(int numWorkers);

    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    WorkerPool(WorkerPool&&) = delete;
    WorkerPool& operator=(WorkerPool&&) = delete;

    /// @brief Number of worker threads (excluding the calling thread).
    [[nodiscard]] int numWorkers() const noexcept { return static_cast<int>(workers_.size()); }

    /// @brief Run @p fn in parallel over the index range [0, count).
    ///
    /// The range is split into `numWorkers + 1` chunks (workers + the
    /// calling thread), each worker receives one chunk, and the calling
    /// thread runs the last chunk inline before waiting for the others.
    /// Blocks until every chunk has returned.
    ///
    /// `fn(begin, end)` is called once per chunk with a half-open range.
    /// Different threads may be running fn concurrently — fn must be
    /// thread-safe with respect to itself, and must not mutate shared
    /// state across chunks without explicit synchronization.
    void parallelFor(int count, const std::function<void(int begin, int end)>& fn);

private:
    void workerLoop(int workerId);

    std::vector<std::thread> workers_;

    // Job hand-off state.  The main thread sets `currentFn_` + `currentTotal_`
    // + `currentChunkSize_` + bumps `generation_`, then notifies all workers.
    // Each worker grabs its `workerId` chunk.  When all workers (and the
    // caller's inline chunk) finish, `outstanding_` reaches zero and the
    // caller is unblocked.
    std::mutex mtx_;
    std::condition_variable cvWork_;
    std::condition_variable cvDone_;
    const std::function<void(int, int)>* currentFn_ = nullptr;
    int currentTotal_ = 0;
    int currentChunkSize_ = 0;
    int currentGeneration_ = 0;
    std::atomic<int> outstanding_{0};
    std::atomic<bool> stop_{false};
};
