#include "LagComp.hpp"

#include <cstdlib>

// ---------------------------------------------------------------------------
// Push a new snapshot into the ring buffer
// ---------------------------------------------------------------------------

void LagComp::push(uint32_t tick, const LagSnapshot::Entry entries[k_maxPlayers], uint8_t playerCount)
{
    head = (head + 1) % k_lagCompTicks;
    auto& snap = ring[head];
    snap.tick = tick;
    snap.count = playerCount;
    for (int i = 0; i < k_maxPlayers; ++i)
        snap.players[i] = entries[i];

    if (count < k_lagCompTicks)
        count++;
}

// ---------------------------------------------------------------------------
// Find the snapshot whose tick is closest to the requested tick
// ---------------------------------------------------------------------------

const LagSnapshot* LagComp::find(uint32_t tick) const
{
    if (count == 0)
        return nullptr;

    const LagSnapshot* best = nullptr;
    uint32_t bestDif = 0xFFFFFFFFu;

    for (int i = 0; i < count; ++i) {
        int idx = (head - i + k_lagCompTicks) % k_lagCompTicks;
        const auto& s = ring[idx];
        uint32_t dif = (s.tick > tick) ? s.tick - tick : tick - s.tick;
        if (dif < bestDif) {
            bestDif = dif;
            best = &s;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Rewind helper — fills outPositions/outAlive arrays
// ---------------------------------------------------------------------------

bool LagComp::rewind(uint32_t tick, glm::vec3 outPositions[k_maxPlayers], bool outAlive[k_maxPlayers]) const
{
    const LagSnapshot* snap = find(tick);
    if (!snap)
        return false;

    for (int i = 0; i < k_maxPlayers; ++i) {
        outPositions[i] = snap->players[i].pos;
        outAlive[i] = snap->players[i].alive;
    }
    return true;
}

uint32_t LagComp::newestTick() const
{
    if (count == 0)
        return 0;
    return ring[head].tick;
}

uint32_t LagComp::oldestTick() const
{
    if (count == 0)
        return 0;
    int oldest = (head - count + 1 + k_lagCompTicks) % k_lagCompTicks;
    return ring[oldest].tick;
}
