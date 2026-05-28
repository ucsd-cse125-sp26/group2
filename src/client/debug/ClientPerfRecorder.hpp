/// @file ClientPerfRecorder.hpp
/// @brief Low-overhead client frame profiler that records play sessions to CSV.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// @brief One rendered client frame worth of timing and workload counters.
///
/// All timing fields are milliseconds.  `wallFrameMs` is the elapsed time since
/// the previous iterate() call, while `cpuFrameMs` is the measured CPU time spent
/// inside the current iterate() call before the optional software limiter.
struct ClientPerfFrame
{
    std::uint64_t frameNumber = 0;
    double timestampMs = 0.0;
    float wallFrameMs = 0.0f;
    float cpuFrameMs = 0.0f;

    float preambleMs = 0.0f;
    float inputMs = 0.0f;
    float networkStatsMs = 0.0f;
    float physicsMs = 0.0f;
    float networkPollMs = 0.0f;
    float snapshotApplyMs = 0.0f;
    float reconciliationMs = 0.0f;
    float refreshPlayersMs = 0.0f;
    float refreshProjectilesMs = 0.0f;
    float refreshRespawnsMs = 0.0f;
    float refreshDroppedWeaponsMs = 0.0f;
    float refreshPowerupsMs = 0.0f;
    float cameraResolveMs = 0.0f;
    float cameraMs = 0.0f;
    float localVfxMs = 0.0f;
    float dispatchMs = 0.0f;
    float particlesMs = 0.0f;
    float audioMs = 0.0f;
    float interpolationMs = 0.0f;
    float animationMs = 0.0f;
    float entityCmdsMs = 0.0f;
    float viewmodelMs = 0.0f;
    float recorderFpsMs = 0.0f;
    float imguiMs = 0.0f;
    float hudMs = 0.0f;
    float pauseMenuMs = 0.0f;
    float imguiRenderMs = 0.0f;
    float drawFrameMs = 0.0f;
    float drawAcquireMs = 0.0f;
    float drawRecordMs = 0.0f;
    float drawSubmitMs = 0.0f;
    float frameLimiterMs = 0.0f;

    std::uint32_t physicsTicks = 0;
    std::uint32_t tickCount = 0;
    std::uint32_t snapshotApplyCount = 0;
    std::uint32_t snapshotApplied = 0;
    std::uint32_t reconcileRequestedTicks = 0;
    std::uint32_t reconcileReplayedTicks = 0;
    std::uint32_t reconcileMissingTicks = 0;
    std::uint32_t reconcileSkippedExact = 0;
    std::uint32_t reconcileReplayForced = 0;
    std::uint32_t reconcileMissingHistory = 0;
    std::uint32_t clientPredictTick = 0;
    std::uint32_t serverAckedClientTick = 0;
    float reconcileErrorPosition = 0.0f;
    float reconcileErrorVelocity = 0.0f;
    float accumulatorMs = 0.0f;
    float measuredPhysicsHz = 0.0f;
    float fpsCurrent = 0.0f;
    float fps1pLow = 0.0f;
    float fps5pLow = 0.0f;

    std::uint32_t playerEntities = 0;
    std::uint32_t localPlayers = 0;
    std::uint32_t renderableEntities = 0;
    std::uint32_t projectileEntities = 0;
    std::uint32_t fireFields = 0;
    std::uint32_t animatedCandidates = 0;
    std::uint32_t animatedSampled = 0;
    std::uint32_t animatedDrawn = 0;
    std::uint32_t skinnedInstances = 0;
    std::uint32_t boneMatrices = 0;
    std::uint32_t entityRenderCmds = 0;
    std::uint32_t pointLights = 0;
    std::uint32_t beamPointLights = 0;

    std::uint32_t impactParticles = 0;
    std::uint32_t tracerParticles = 0;
    std::uint32_t ribbonVertices = 0;
    std::uint32_t hitscanBeams = 0;
    std::uint32_t arcVertices = 0;
    std::uint32_t smokeParticles = 0;
    std::uint32_t decals = 0;

    std::uint32_t audioSourcesActive = 0;
    std::uint32_t voiceSourcesActive = 0;
    std::uint64_t audioEventsPosted = 0;
    std::uint64_t audioCommandsGenerated = 0;
    std::uint64_t audioSourcesStarted = 0;
    std::uint64_t audioDroppedByCooldown = 0;
    std::uint64_t audioDroppedByLimit = 0;
    std::uint64_t audioStolenSources = 0;

    float rttMs = 0.0f;
    float avgRttMs = 0.0f;
    float recvKBps = 0.0f;
    float sendKBps = 0.0f;
    float registryUpdateKB = 0.0f;

    std::uint32_t swapchainWidth = 0;
    std::uint32_t swapchainHeight = 0;
    std::uint32_t rendererWorldInstances = 0;
    std::uint32_t rendererEntityCmds = 0;
    std::uint32_t rendererEntityDraws = 0;
    std::uint32_t rendererPointLights = 0;
    std::uint32_t rendererSkinnedInstances = 0;
    std::uint32_t rendererWeaponDrawn = 0;
    std::uint32_t rendererModelDraws = 0;
    std::uint32_t rendererMeshDraws = 0;
    std::uint32_t rendererIndexedDraws = 0;
    std::uint32_t rendererTriangles = 0;

    std::uint32_t imguiDrawLists = 0;
    std::uint32_t imguiVertices = 0;
    std::uint32_t imguiIndices = 0;

    std::uint32_t perfMovementCalls = 0;
    std::uint32_t perfMovementPlayers = 0;
    std::uint32_t perfCollisionCalls = 0;
    std::uint32_t perfCollisionPlayers = 0;
    std::uint32_t perfKccCalls = 0;
    std::uint32_t perfKccBumpHits = 0;
    std::uint32_t perfKccCaIterations = 0;
    std::uint32_t perfKccSweepHits = 0;
    std::uint32_t perfWallDetectCalls = 0;
    std::uint32_t perfWallMeshProbes = 0;
    std::uint32_t perfWallMeshProbeMeshes = 0;
    std::uint32_t perfWallSphereFallbacks = 0;
    std::uint32_t perfWallAttachmentCalls = 0;
    std::uint32_t perfWallAttachmentMeshes = 0;
    std::uint32_t perfWallDetectSkippedByGate = 0;
    std::uint32_t perfWallAttachmentPrevTriangleHits = 0;
    std::uint32_t perfWallAttachmentNeighborHits = 0;
    std::uint32_t perfWallAttachmentBroadphaseFallbacks = 0;
    std::uint32_t perfStaticBroadphaseQueries = 0;
    std::uint32_t perfStaticBroadphaseMeshes = 0;
    std::uint32_t perfSweepAabbAllCalls = 0;
    std::uint32_t perfSweepCapsuleAllCalls = 0;
    std::uint32_t perfSweepCapsuleTriMeshCalls = 0;
    std::uint32_t perfSweepCapsuleTriMeshNodes = 0;
    std::uint32_t perfSweepCapsuleTriMeshTris = 0;
    std::uint32_t perfDeepestCapsuleCalls = 0;
    std::uint32_t perfDeepestCapsuleTriMeshCalls = 0;
    std::uint32_t perfDeepestCapsuleTriMeshNodes = 0;
    std::uint32_t perfDeepestCapsuleTriMeshTris = 0;
    std::uint32_t perfClosestPointMeshCalls = 0;
    std::uint32_t perfClosestPointMeshNodes = 0;
    std::uint32_t perfClosestPointMeshTris = 0;
    std::uint32_t perfClosestPointTriangleCalls = 0;
    std::uint32_t perfClosestPointWallProbeCalls = 0;
    std::uint32_t perfClosestPointWallProbeNodes = 0;
    std::uint32_t perfClosestPointWallProbeTris = 0;
    std::uint32_t perfClosestPointWallAttachmentCalls = 0;
    std::uint32_t perfClosestPointWallAttachmentNodes = 0;
    std::uint32_t perfClosestPointWallAttachmentTris = 0;
};

/// @brief Session recorder enabled by GROUP2_CLIENT_PERF=1.
///
/// The recorder does not write from the frame loop.  Frames are appended to a
/// pre-reserved vector and flushed to disk on quit, keeping the measured stutter
/// profile free from profiler file I/O.
class ClientPerfRecorder
{
public:
    void configureFromEnv(const char* basePath);
    void start();
    void stop();
    void record(const ClientPerfFrame& frame);

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }
    [[nodiscard]] bool isRecording() const noexcept { return recording_; }
    [[nodiscard]] const std::string& sessionDir() const noexcept { return sessionDir_; }

private:
    bool enabled_ = false;
    bool recording_ = false;
    std::size_t reserveFrames_ = 240000;
    std::uint64_t framesBeyondInitialReserve_ = 0;
    std::string baseDir_;
    std::string sessionDir_;
    std::vector<ClientPerfFrame> frames_;

    void writeFramesCsv() const;
    void writeSummary() const;
};
