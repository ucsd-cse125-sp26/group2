/// @file ParticlePool.hpp
/// @brief Fixed-capacity particle pool with O(1) swap-remove.

#pragma once

#include <array>
#include <concepts>
#include <cstdint>

/// @brief Fixed-capacity particle pool with O(1) swap-remove.
///
/// Particles are stored in a contiguous array[0..count-1].
/// Calling kill(i) swaps data[i] with data[count-1] and decrements count —
/// stable ordering is NOT preserved (intentional; no sort needed for upload).
///
/// Usage:
///   auto* p = pool.spawn();    // nullptr when full
///   pool.update([](T& p) -> bool { return p.lifetime > 0; });
///   pool.kill(i);
template <typename T, uint32_t MaxN>
struct ParticlePool
{
    std::array<T, MaxN> data{};
    uint32_t count = 0;

    /// @brief Allocate a new slot (zero-initialised). Returns nullptr when full.
    T* spawn()
    {
        if (count >= MaxN)
            return nullptr;
        data[count] = T{};
        return &data[count++];
    }

    /// @brief O(1) swap-remove. Does NOT preserve order.
    void kill(uint32_t i) { data[i] = data[--count]; }

    /// @brief Iterate all live particles. fn(T& p) → bool: return false to kill.
    /// Iterates backwards so kill() inside fn doesn't skip elements.
    void update(auto fn)
    {
        for (uint32_t i = count; i-- > 0;)
            if (!fn(data[i]))
                kill(i);
    }

    /// @brief Pointer to the contiguous live data for GPU upload.
    [[nodiscard]] const T* rawData() const { return data.data(); }
    [[nodiscard]] uint32_t liveCount() const { return count; }
    [[nodiscard]] bool empty() const { return count == 0; }
    [[nodiscard]] bool full() const { return count >= MaxN; }
};
