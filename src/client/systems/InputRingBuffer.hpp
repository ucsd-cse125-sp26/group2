/// @file InputRingBuffer.hpp
/// @brief Per-tick history of stamped input snapshots, keyed by clientPredictTick.
///
/// Phase 5b uses this to replay inputs during reconciliation. When a server
/// snapshot arrives ack'ing client-tick T, we know the server's
/// authoritative state at tick T. We snap the local player to that state
/// and replay every stored input from tick T+1 up through the current
/// predict tick — feeding each one into runMovement + runCollision —
/// landing on the correct predicted state for the *current* tick.
///
/// Capacity: 256 entries ≈ 2 s of input history at 128 Hz physics. Far more
/// than typical RTT (10–200 ms = 1–26 ticks); the slack absorbs spikes.
///
/// Ring writes are O(1); lookup by tick is O(N) but N is bounded at 256
/// and the access pattern is "find the most recent entry with tick ≥ T",
/// which usually walks just a handful of slots.

#pragma once

#include "ecs/components/InputSnapshot.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

class InputRingBuffer
{
public:
    /// @brief Capacity in ticks. Sized for ≥1 RTT of replay buffer at any
    /// reasonable RTT; oversize is safer than too-small (replay just stops
    /// early if history runs out).
    static constexpr size_t k_capacity = 256;

    struct Entry
    {
        uint32_t tick = 0;     ///< clientPredictTick when this input was stamped + sent.
        InputSnapshot input{}; ///< The input as sampled (and as sent to the server).
        bool valid = false;    ///< False for default-constructed slots that never got pushed.
    };

    /// @brief Push the input that was just stamped + sent for @p tick.
    void push(uint32_t tick, const InputSnapshot& input)
    {
        entries_[head_] = Entry{.tick = tick, .input = input, .valid = true};
        head_ = (head_ + 1) % k_capacity;
        if (count_ < k_capacity)
            ++count_;
    }

    /// @brief Find the input stamped with exactly @p tick.
    /// @return Pointer into the ring (valid until next push), or nullptr if missing.
    [[nodiscard]] const InputSnapshot* find(uint32_t tick) const
    {
        // Walk newest-to-oldest. Most replays start near the head so this
        // is typically a 1-2 step search; worst case k_capacity steps.
        for (size_t i = 0; i < count_; ++i) {
            const size_t idx = (head_ + k_capacity - 1 - i) % k_capacity;
            const Entry& e = entries_[idx];
            if (!e.valid)
                continue;
            if (e.tick == tick)
                return &e.input;
            if (e.tick < tick)
                break; // ring is monotonically increasing — no point searching further
        }
        return nullptr;
    }

    /// @brief Number of valid entries currently buffered (≤ k_capacity).
    [[nodiscard]] size_t size() const noexcept { return count_; }

    /// @brief Drop everything (used on disconnect / reset).
    void clear() noexcept
    {
        head_ = 0;
        count_ = 0;
        for (auto& e : entries_)
            e.valid = false;
    }

private:
    std::array<Entry, k_capacity> entries_{};
    size_t head_ = 0;  ///< Next write index, wraps mod k_capacity.
    size_t count_ = 0; ///< Number of valid entries; saturates at k_capacity.
};
