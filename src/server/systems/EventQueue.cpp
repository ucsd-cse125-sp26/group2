/// @file EventQueue.cpp
/// @brief Implementation of the EventQueue FIFO operations.

#include "EventQueue.hpp"

#include <cstddef>
#include <stdexcept>

namespace
{
constexpr std::size_t k_maxQueuedEvents = 8192;
}

bool EventQueue::isEmpty()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    return events.empty();
}

void EventQueue::enqueue(Event event)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    if (events.size() >= k_maxQueuedEvents)
        events.pop();
    events.push(event);
}

Event EventQueue::dequeue()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    if (events.empty()) {
        throw std::runtime_error("EventQueue: cannot dequeue from empty queue");
    }
    Event event = events.front();
    events.pop();
    return event;
}

int EventQueue::size()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    return static_cast<int>(events.size());
}

void EventQueue::drainAll(std::vector<Event>& out)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    out.clear();
    if (events.empty())
        return;
    out.reserve(out.capacity() < events.size() ? events.size() : out.capacity());
    while (!events.empty()) {
        out.push_back(events.front());
        events.pop();
    }
}
