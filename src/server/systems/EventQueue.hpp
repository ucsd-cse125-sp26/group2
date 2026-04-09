#include <queue>
#include <utility>

struct Event
{
    int clientId;
    std::pair<float, float> moveIntentVector;
    bool jumpIntent;
    bool shootIntent;
};

class EventQueue
{
public:
    bool isEmpty();
    void enqueue(Event event);
    Event dequeue();

private:
    std::queue<Event> events;
};
