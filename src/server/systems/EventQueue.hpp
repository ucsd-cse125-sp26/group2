#pragma once
#include <glm/vec2.hpp>
#include <queue>
#include <stdexcept>

class MovementIntent
{
public:
    bool forward;
    bool back;
    bool left;
    bool right;
    bool jump;
    bool crouch;

    float yaw;
    float pitch;
    float roll;
};

class Event
{
public:
    int clientId;
    MovementIntent movementIntent;
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
