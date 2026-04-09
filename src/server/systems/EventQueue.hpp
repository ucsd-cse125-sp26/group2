#pragma once
#include <glm/vec2.hpp>
#include <queue>

class Event
{
public:
    int clientId;
    glm::vec2 moveIntentVector;
    bool jumpIntent;
    bool shootIntent;
};

class EventQueue
{
public:
    bool isEmpty();
    void enqueue(Event event);
    Event dequeue();
    int size();

private:
    std::queue<Event> events;
};
