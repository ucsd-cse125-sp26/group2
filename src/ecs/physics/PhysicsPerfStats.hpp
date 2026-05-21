/// @file PhysicsPerfStats.hpp
/// @brief Optional per-frame physics/KCC counters for client performance captures.

#pragma once

#include <cstdint>

namespace physics::perf
{

struct FrameStats
{
    std::uint32_t movementCalls = 0;
    std::uint32_t movementPlayers = 0;
    std::uint32_t collisionCalls = 0;
    std::uint32_t collisionPlayers = 0;

    std::uint32_t kccCalls = 0;
    std::uint32_t kccBumpHits = 0;
    std::uint32_t kccCaIterations = 0;
    std::uint32_t kccSweepHits = 0;

    std::uint32_t wallDetectCalls = 0;
    std::uint32_t wallMeshProbes = 0;
    std::uint32_t wallMeshProbeMeshes = 0;
    std::uint32_t wallSphereFallbacks = 0;
    std::uint32_t wallAttachmentCalls = 0;
    std::uint32_t wallAttachmentMeshes = 0;
    std::uint32_t wallDetectSkippedByGate = 0;
    std::uint32_t wallAttachmentPrevTriangleHits = 0;
    std::uint32_t wallAttachmentNeighborHits = 0;
    std::uint32_t wallAttachmentBroadphaseFallbacks = 0;

    std::uint32_t staticBroadphaseQueries = 0;
    std::uint32_t staticBroadphaseMeshes = 0;

    std::uint32_t sweepAabbAllCalls = 0;
    std::uint32_t sweepCapsuleAllCalls = 0;
    std::uint32_t sweepCapsuleTriMeshCalls = 0;
    std::uint32_t sweepCapsuleTriMeshNodes = 0;
    std::uint32_t sweepCapsuleTriMeshTris = 0;

    std::uint32_t deepestCapsuleCalls = 0;
    std::uint32_t deepestCapsuleTriMeshCalls = 0;
    std::uint32_t deepestCapsuleTriMeshNodes = 0;
    std::uint32_t deepestCapsuleTriMeshTris = 0;

    std::uint32_t closestPointMeshCalls = 0;
    std::uint32_t closestPointMeshNodes = 0;
    std::uint32_t closestPointMeshTris = 0;
    std::uint32_t closestPointTriangleCalls = 0;

    std::uint32_t closestPointWallProbeCalls = 0;
    std::uint32_t closestPointWallProbeNodes = 0;
    std::uint32_t closestPointWallProbeTris = 0;
    std::uint32_t closestPointWallAttachmentCalls = 0;
    std::uint32_t closestPointWallAttachmentNodes = 0;
    std::uint32_t closestPointWallAttachmentTris = 0;
};

inline thread_local bool enabled = false;
inline thread_local FrameStats frame;

enum class ClosestPointContext : std::uint8_t
{
    Generic,
    WallProbe,
    WallAttachment,
};

inline thread_local ClosestPointContext closestPointContext = ClosestPointContext::Generic;

class ScopedClosestPointContext
{
public:
    explicit ScopedClosestPointContext(ClosestPointContext context) noexcept : previous_(closestPointContext)
    {
        closestPointContext = context;
    }

    ~ScopedClosestPointContext() { closestPointContext = previous_; }

private:
    ClosestPointContext previous_;
};

inline void setEnabled(bool value) noexcept
{
    enabled = value;
}

[[nodiscard]] inline bool isEnabled() noexcept
{
    return enabled;
}

inline void resetFrame() noexcept
{
    frame = {};
}

[[nodiscard]] inline FrameStats snapshot() noexcept
{
    return frame;
}

inline void add(std::uint32_t FrameStats::*field, std::uint32_t amount = 1) noexcept
{
    if (enabled)
        frame.*field += amount;
}

inline void addClosestPointCall() noexcept
{
    add(&FrameStats::closestPointMeshCalls);
    if (!enabled)
        return;
    if (closestPointContext == ClosestPointContext::WallProbe)
        ++frame.closestPointWallProbeCalls;
    else if (closestPointContext == ClosestPointContext::WallAttachment)
        ++frame.closestPointWallAttachmentCalls;
}

inline void addClosestPointNode() noexcept
{
    add(&FrameStats::closestPointMeshNodes);
    if (!enabled)
        return;
    if (closestPointContext == ClosestPointContext::WallProbe)
        ++frame.closestPointWallProbeNodes;
    else if (closestPointContext == ClosestPointContext::WallAttachment)
        ++frame.closestPointWallAttachmentNodes;
}

inline void addClosestPointTri() noexcept
{
    add(&FrameStats::closestPointMeshTris);
    if (!enabled)
        return;
    if (closestPointContext == ClosestPointContext::WallProbe)
        ++frame.closestPointWallProbeTris;
    else if (closestPointContext == ClosestPointContext::WallAttachment)
        ++frame.closestPointWallAttachmentTris;
}

} // namespace physics::perf
