#include "EventQueue.hpp"

bool EventQueue::isEmpty()
{
    return events.empty();
}

void EventQueue::enqueue(Event event)
{
    events.push(event);
}

Event EventQueue::dequeue()
{
    if (events.empty()) {
        throw std::runtime_error("EventQueue: cannot dequeue from empty queue");
    }
    Event event = events.front();
    events.pop();
    return event;
}

int EventQueue::size()
{
    return static_cast<int>(events.size());
}
