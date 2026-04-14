/// @file EventQueue.hpp
/// @brief Thread-safe event queue for passing network events to the game loop.

#pragma once
#include "ecs/components/ClientId.hpp"
#include "ecs/components/InputSnapshot.hpp"

#include <queue>

/// @brief A single gameplay event produced by network input processing.
class Event
{
public:
    ClientId clientId;            ///< Originating client identifier.
    InputSnapshot movementIntent; ///< Decoded movement fields.
};

/// @brief FIFO queue of gameplay events awaiting processing each tick.
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

private:
    std::queue<Event> events; ///< Underlying FIFO storage.
};
