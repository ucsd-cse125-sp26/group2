#include "server/game/LagCompMath.hpp"

#include <cstdint>
#include <cstdlib>

namespace
{
void require(bool condition)
{
    if (!condition)
        std::abort();
}

using server::lagcomp::computeRewindTicks;
using server::lagcomp::msToTicks;

// Round-to-nearest ms→tick conversion at the default 128 Hz physics rate.
void testMsToTicksRounding()
{
    require(msToTicks(0, 128) == 0);
    require(msToTicks(3, 128) == 0);  // 3*128+500   = 884   → 0
    require(msToTicks(4, 128) == 1);  // 4*128+500   = 1012  → 1
    require(msToTicks(31, 128) == 4); // 31*128+500  = 4468  → 4
    require(msToTicks(63, 128) == 8); // 63*128+500  = 8564  → 8  (~62.5 ms = 8 ticks)
    require(msToTicks(100, 128) == 13);
}

// When the client reports a measured delay (ms > 0), it is used verbatim and
// the snapshot-count fallback is ignored — even when the two disagree.
void testPrefersMeasuredMsOverSnapshotCount()
{
    // Measured 31 ms (~4 ticks) while the snapshot-count path would yield
    // 2 × 4 = 8 ticks.  The ms value must win.
    const std::uint32_t ticks = computeRewindTicks(/*rttMs*/ 0,
                                                   /*interpDelayMs*/ 31,
                                                   /*interpDelaySnapshots*/ 2,
                                                   /*snapshotEveryNTicks*/ 4,
                                                   /*tickRateHz*/ 128,
                                                   /*maxLagCompTicks*/ 64);
    require(ticks == 4);
}

// When the client reports 0 ms (interp disabled or pre-PR-31 client), the
// server falls back to snapshots × nominal cadence.
void testFallbackWhenMsZero()
{
    const std::uint32_t ticks = computeRewindTicks(0, /*interpDelayMs*/ 0, 2, 4, 128, 64);
    require(ticks == 8); // 2 × 4
}

// RTT and interp terms add (both via the same round-to-nearest path).
void testRttPlusInterpAdd()
{
    const std::uint32_t ticks = computeRewindTicks(/*rttMs*/ 100, /*interpDelayMs*/ 63, 2, 4, 128, 64);
    require(ticks == msToTicks(100, 128) + msToTicks(63, 128)); // 13 + 8 = 21
}

// The combined depth is clamped to the ring size.
void testClampToMax()
{
    const std::uint32_t ticks = computeRewindTicks(60000, 60000, 8, 4, 128, 64);
    require(ticks == 64);
}

// The point of PR-31: under jitter the client's measured delay exceeds the
// nominal cadence, so the rewind grows to match what the client actually saw
// instead of staying pinned to the (too-small) nominal estimate.
void testJitterDriftTracksMeasuredDelay()
{
    const std::uint32_t nominal = computeRewindTicks(0, /*ms*/ 0, 2, 4, 128, 64);  // 8 ticks (62.5 ms)
    const std::uint32_t jittered = computeRewindTicks(0, /*ms*/ 80, 2, 4, 128, 64); // 10 ticks
    require(nominal == 8);
    require(jittered == 10);
    require(jittered > nominal);
}

// snapshotEveryNTicks of 0 must not divide-by-zero / collapse the fallback.
void testFallbackGuardsZeroCadence()
{
    const std::uint32_t ticks = computeRewindTicks(0, 0, 2, /*snapshotEveryNTicks*/ 0, 128, 64);
    require(ticks == 2); // 2 × max(1, 0)
}
} // namespace

int main()
{
    testMsToTicksRounding();
    testPrefersMeasuredMsOverSnapshotCount();
    testFallbackWhenMsZero();
    testRttPlusInterpAdd();
    testClampToMax();
    testJitterDriftTracksMeasuredDelay();
    testFallbackGuardsZeroCadence();
    return 0;
}
