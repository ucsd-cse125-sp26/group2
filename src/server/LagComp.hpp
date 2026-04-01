#pragma once
#include "../net/Protocol.hpp"

#include <array>
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// LagComp — server-side snapshot ring buffer for lag compensation.
//
// Every server tick we push the current positions of all players.
// On a hitscan shot the server rewinds player hitboxes to the tick that
// matches the client's view time, checks the ray, then restores them.
// ---------------------------------------------------------------------------

struct LagSnapshot
{
    uint32_t tick = 0;
    uint8_t count = 0;

    struct Entry
    {
        glm::vec3 pos; // feet center (physics origin)
        bool alive = false;
    };
    Entry players[k_maxPlayers];
};

class LagComp
{
public:
    // Capsule geometry (same for every player for now)
    static constexpr float k_capsuleRadius     = 32.0f; // qu (= 0.4 m)
    static constexpr float k_capsuleHalfHeight = 36.0f; // qu (= 0.45 m)

    // Push the current frame into the ring buffer.
    void push(uint32_t tick, const LagSnapshot::Entry entries[k_maxPlayers], uint8_t count);

    // Return a pointer to the snapshot closest to the requested tick.
    // Returns nullptr if the history is empty.
    const LagSnapshot* find(uint32_t tick) const;

    // Rewind all player positions in `outPositions` to the snapshot at `tick`.
    // Returns false if no suitable snapshot found.
    bool rewind(uint32_t tick, glm::vec3 outPositions[k_maxPlayers], bool outAlive[k_maxPlayers]) const;

    uint32_t newestTick() const;
    uint32_t oldestTick() const;

private:
    std::array<LagSnapshot, k_lagCompTicks> ring;
    int head  = 0; // index of most recently written entry
    int count = 0; // number of valid entries
};
