/// @file EventQueue.hpp
/// @brief Thread-safe event queue for passing network events to the game loop.

#pragma once
#include <queue>

/// @brief Decoded movement intent extracted from a client input packet.
class MovementIntent
{
public:
    bool forward; ///< Moving forward.
    bool back;    ///< Moving backward.
    bool left;    ///< Strafing left.
    bool right;   ///< Strafing right.
    bool jump;    ///< Jump requested.
    bool crouch;  ///< Crouch requested.

    float yaw;    ///< Horizontal look angle in degrees.
    float pitch;  ///< Vertical look angle in degrees.
    float roll;   ///< Roll angle in degrees.
};

/// @brief A single gameplay event produced by network input processing.
class Event
{
public:
    int clientId;                  ///< Originating client identifier.
    MovementIntent movementIntent; ///< Decoded movement fields.
    bool shootIntent;              ///< True if the client is firing.
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
