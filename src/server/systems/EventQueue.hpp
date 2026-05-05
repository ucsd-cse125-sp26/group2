/// @file EventQueue.hpp
/// @brief Thread-safe event queue for passing network events to the game loop.

#pragma once
#include "Event.hpp"

#include <mutex>
#include <queue>
#include <vector>

/// @brief Thread-safe FIFO queue of gameplay events awaiting processing each tick.
///
/// PR-2c (server-perf): the queue holds its own mutex now. Pre-PR-2c
/// the Server's `stateMutex_` doubled as the eventQueue lock — every
/// `enqueue` (called inside `readClients`, on the network thread) and
/// every `drainAll` (called from `tick()`, on the game thread)
/// acquired the *same* mutex that protects the clients map. Result:
/// the game thread's `drainEvents` blocked on the network thread's
/// `readClients` per-cycle work, contributing ~12 ms p99 to tick
/// time at 100 bots. With this mutex, the two paths only compete on
/// the queue itself — a much shorter critical section.
class EventQueue
{
public:
    /// @brief Check whether the queue contains no events.
    /// @return True if the queue is empty.
    bool isEmpty();

    /// @brief Push an event onto the back of the queue.
    /// @param event The event to enqueue.
    void enqueue(Event event);

    /// @brief Remove and return the front event.
    /// @return The oldest pending event.
    Event dequeue();

    /// @brief Return the number of pending events.
    /// @return Queue size.
    int size();

    /// @brief PR-2b (server-perf): drain every queued event into @p out
    /// in FIFO order. Single-pass; the queue is empty afterwards.
    /// Used by the game thread to avoid per-event lock acquisition
    /// against the network thread's enqueue path.
    void drainAll(std::vector<Event>& out);

private:
    std::queue<Event> events;      ///< Underlying FIFO storage.
    mutable std::mutex queueMutex; ///< Guards `events` for cross-thread access.
};
