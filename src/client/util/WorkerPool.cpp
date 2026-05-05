/// @file WorkerPool.cpp
/// @brief Implementation of the tiny persistent parallel-for thread pool.

#include "WorkerPool.hpp"

#include <algorithm>
#include <cstdlib>

WorkerPool::WorkerPool(int numWorkers)
{
    if (numWorkers < 0)
        numWorkers = 0;
    workers_.reserve(static_cast<size_t>(numWorkers));
    for (int i = 0; i < numWorkers; ++i)
        workers_.emplace_back([this, i] { workerLoop(i); });
}

WorkerPool::~WorkerPool()
{
    {
        std::unique_lock<std::mutex> lock(mtx_);
        stop_.store(true, std::memory_order_release);
        ++currentGeneration_;
    }
    cvWork_.notify_all();
    for (auto& t : workers_)
        if (t.joinable())
            t.join();
}

void WorkerPool::parallelFor(int count, const std::function<void(int, int)>& fn)
{
    if (count <= 0)
        return;

    // Per-job dispatch overhead is ~5–10 µs (cv notify + wake N workers +
    // 2 mutex round-trips).  With micro-jobs that's pure waste — running
    // inline is faster.  k_minParallelCount is calibrated for ~1 µs of
    // work per item; bump it if your items are heavier (animation pose
    // sample is closer to 5 µs and would benefit at ~50 items, but the
    // 100-bot-30-visible scenario sits below that anyway).
    // Default 256; override at runtime with GROUP2_PARALLEL_THRESHOLD for
    // experimentation (e.g. 16 to verify the parallel path runs at all on
    // smaller workloads, or 1024 for very-heavy single-item work).
    static const int k_minParallelCount = []() {
        if (const char* p = std::getenv("GROUP2_PARALLEL_THRESHOLD")) {
            char* end = nullptr;
            const long n = std::strtol(p, &end, 10);
            if (*end == '\0' && n > 0)
                return static_cast<int>(n);
        }
        return 256;
    }();
    const int totalWorkers = static_cast<int>(workers_.size()) + 1;
    if (totalWorkers <= 1 || count <= totalWorkers || count < k_minParallelCount) {
        fn(0, count);
        return;
    }

    const int chunkSize = (count + totalWorkers - 1) / totalWorkers;
    const int workerChunks = totalWorkers - 1; // last chunk runs on caller

    {
        std::unique_lock<std::mutex> lock(mtx_);
        currentFn_ = &fn;
        currentTotal_ = count;
        currentChunkSize_ = chunkSize;
        outstanding_.store(workerChunks, std::memory_order_release);
        ++currentGeneration_;
    }
    cvWork_.notify_all();

    // Run the caller's chunk inline (the last chunk so the indices below
    // line up with the worker IDs).
    {
        const int begin = workerChunks * chunkSize;
        const int end = std::min(begin + chunkSize, count);
        if (begin < end)
            fn(begin, end);
    }

    // Wait for the workers to finish.
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cvDone_.wait(lock, [&] { return outstanding_.load(std::memory_order_acquire) == 0; });
    }
}

void WorkerPool::workerLoop(int workerId)
{
    int lastGen = 0;
    while (true) {
        // Wait for a new job (or stop signal).
        std::function<void(int, int)> fn;
        int total = 0;
        int chunkSize = 0;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cvWork_.wait(lock, [&] { return stop_.load(std::memory_order_acquire) || currentGeneration_ != lastGen; });
            if (stop_.load(std::memory_order_acquire))
                return;
            lastGen = currentGeneration_;
            if (!currentFn_)
                continue;
            fn = *currentFn_;
            total = currentTotal_;
            chunkSize = currentChunkSize_;
        }

        // Compute this worker's chunk and run it.
        const int begin = workerId * chunkSize;
        const int end = std::min(begin + chunkSize, total);
        if (begin < end && fn)
            fn(begin, end);

        // Mark our chunk done.  When the last worker decrements the counter
        // to zero, notify the waiter (the caller of parallelFor).
        if (outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::unique_lock<std::mutex> lock(mtx_);
            cvDone_.notify_one();
        }
    }
}
