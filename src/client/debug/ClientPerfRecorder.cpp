/// @file ClientPerfRecorder.cpp
/// @brief CSV and summary output for the client frame profiler.

#include "ClientPerfRecorder.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string_view>
#include <system_error>

namespace
{
bool envEnabled(const char* value)
{
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

std::size_t parseSizeEnv(const char* value, std::size_t fallback)
{
    if (!value || !*value)
        return fallback;
    std::size_t parsed = 0;
    const std::string_view text(value);
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end)
        return fallback;
    return parsed;
}

std::string makeTimestamp()
{
    std::time_t t = std::time(nullptr);
    std::tm* tmPtr = std::localtime(&t); // NOLINT(concurrency-mt-unsafe)
    std::ostringstream oss;
    oss << std::put_time(tmPtr, "%Y%m%d_%H%M%S");
    return oss.str();
}

float percentileMs(std::vector<float>& sorted, float p)
{
    if (sorted.empty())
        return 0.0f;
    const std::size_t idx = static_cast<std::size_t>(static_cast<float>(sorted.size() - 1) * p);
    return sorted[idx];
}

float average(const std::vector<float>& values)
{
    if (values.empty())
        return 0.0f;
    return std::accumulate(values.begin(), values.end(), 0.0f) / static_cast<float>(values.size());
}

void writeHeader(std::ofstream& out)
{
    out << "frame,timestamp_ms,wall_frame_ms,cpu_frame_ms,"
           "preamble_ms,input_ms,network_stats_ms,physics_ms,network_poll_ms,snapshot_apply_ms,"
           "reconciliation_ms,refresh_players_ms,refresh_projectiles_ms,refresh_respawns_ms,"
           "refresh_dropped_weapons_ms,refresh_powerups_ms,camera_resolve_ms,camera_ms,local_vfx_ms,"
           "dispatch_ms,particles_ms,audio_ms,interpolation_ms,animation_ms,entity_cmds_ms,viewmodel_ms,"
           "recorder_fps_ms,imgui_ms,hud_ms,pause_menu_ms,imgui_render_ms,draw_frame_ms,draw_acquire_ms,"
           "draw_record_ms,draw_submit_ms,renderer_swapchain_acquire_ms,renderer_depth_ensure_ms,"
           "renderer_camera_update_ms,renderer_static_batch_rebuild_ms,renderer_skinned_upload_ms,"
           "renderer_geometry_pass_ms,renderer_weapon_pass_ms,renderer_ui_pass_ms,renderer_imgui_prepare_ms,"
           "renderer_hud_draw_ms,renderer_imgui_draw_ms,frame_limiter_ms,"
           "physics_ticks,tick_count,snapshot_apply_count,snapshot_applied,reconcile_requested_ticks,"
           "reconcile_replayed_ticks,reconcile_missing_ticks,reconcile_skipped_exact,reconcile_replay_forced,"
           "reconcile_missing_history,client_predict_tick,server_acked_client_tick,reconcile_error_position,"
           "reconcile_error_velocity,accumulator_ms,measured_physics_hz,fps_current,fps_1p_low,fps_5p_low,"
           "player_entities,local_players,renderable_entities,projectile_entities,fire_fields,"
           "animated_candidates,animated_sampled,animated_drawn,skinned_instances,bone_matrices,"
           "entity_render_cmds,point_lights,beam_point_lights,"
           "impact_particles,tracer_particles,ribbon_vertices,hitscan_beams,arc_vertices,smoke_particles,decals,"
           "audio_sources_active,voice_sources_active,audio_events_posted,audio_commands_generated,"
           "audio_sources_started,audio_dropped_by_cooldown,audio_dropped_by_limit,audio_stolen_sources,"
           "rtt_ms,avg_rtt_ms,recv_kbps,send_kbps,registry_update_kb,"
           "swapchain_width,swapchain_height,renderer_world_instances,renderer_entity_cmds,renderer_entity_draws,"
           "renderer_point_lights,renderer_skinned_instances,renderer_weapon_drawn,renderer_model_draws,"
           "renderer_mesh_draws,renderer_indexed_draws,renderer_triangles,renderer_static_batch_draws,"
           "renderer_dynamic_draws,renderer_material_binds,renderer_texture_binds,renderer_static_triangles,"
           "renderer_present_mode,renderer_frame_submitted,renderer_swapchain_skipped,"
           "imgui_draw_lists,imgui_vertices,imgui_indices,"
           "perf_movement_calls,perf_movement_players,perf_collision_calls,perf_collision_players,"
           "perf_kcc_calls,perf_kcc_bump_hits,perf_kcc_ca_iterations,perf_kcc_sweep_hits,"
           "perf_wall_detect_calls,perf_wall_mesh_probes,perf_wall_mesh_probe_meshes,perf_wall_sphere_fallbacks,"
           "perf_wall_attachment_calls,perf_wall_attachment_meshes,perf_wall_detect_skipped_by_gate,"
           "perf_wall_attachment_prev_triangle_hits,perf_wall_attachment_neighbor_hits,"
           "perf_wall_attachment_broadphase_fallbacks,perf_static_broadphase_queries,"
           "perf_static_broadphase_meshes,perf_sweep_aabb_all_calls,perf_sweep_capsule_all_calls,"
           "perf_sweep_capsule_trimesh_calls,perf_sweep_capsule_trimesh_nodes,perf_sweep_capsule_trimesh_tris,"
           "perf_deepest_capsule_calls,perf_deepest_capsule_trimesh_calls,perf_deepest_capsule_trimesh_nodes,"
           "perf_deepest_capsule_trimesh_tris,perf_closest_point_mesh_calls,perf_closest_point_mesh_nodes,"
           "perf_closest_point_mesh_tris,perf_closest_point_triangle_calls,perf_closest_point_wall_probe_calls,"
           "perf_closest_point_wall_probe_nodes,perf_closest_point_wall_probe_tris,"
           "perf_closest_point_wall_attachment_calls,perf_closest_point_wall_attachment_nodes,"
           "perf_closest_point_wall_attachment_tris\n";
}

void writeFrame(std::ofstream& out, const ClientPerfFrame& f)
{
    out << f.frameNumber << ',' << f.timestampMs << ',' << f.wallFrameMs << ',' << f.cpuFrameMs << ',' << f.preambleMs
        << ',' << f.inputMs << ',' << f.networkStatsMs << ',' << f.physicsMs << ',' << f.networkPollMs << ','
        << f.snapshotApplyMs << ',' << f.reconciliationMs << ',' << f.refreshPlayersMs << ',' << f.refreshProjectilesMs
        << ',' << f.refreshRespawnsMs << ',' << f.refreshDroppedWeaponsMs << ',' << f.refreshPowerupsMs << ','
        << f.cameraResolveMs << ',' << f.cameraMs << ',' << f.localVfxMs << ',' << f.dispatchMs << ',' << f.particlesMs
        << ',' << f.audioMs << ',' << f.interpolationMs << ',' << f.animationMs << ',' << f.entityCmdsMs << ','
        << f.viewmodelMs << ',' << f.recorderFpsMs << ',' << f.imguiMs << ',' << f.hudMs << ',' << f.pauseMenuMs << ','
        << f.imguiRenderMs << ',' << f.drawFrameMs << ',' << f.drawAcquireMs << ',' << f.drawRecordMs << ','
        << f.drawSubmitMs << ',' << f.rendererSwapchainAcquireMs << ',' << f.rendererDepthEnsureMs << ','
        << f.rendererCameraUpdateMs << ',' << f.rendererStaticBatchRebuildMs << ',' << f.rendererSkinnedUploadMs << ','
        << f.rendererGeometryPassMs << ',' << f.rendererWeaponPassMs << ',' << f.rendererUiPassMs << ','
        << f.rendererImguiPrepareMs << ',' << f.rendererHudDrawMs << ',' << f.rendererImguiDrawMs << ','
        << f.frameLimiterMs << ',' << f.physicsTicks << ',' << f.tickCount << ',' << f.snapshotApplyCount << ','
        << f.snapshotApplied << ',' << f.reconcileRequestedTicks << ',' << f.reconcileReplayedTicks << ','
        << f.reconcileMissingTicks << ',' << f.reconcileSkippedExact << ',' << f.reconcileReplayForced << ','
        << f.reconcileMissingHistory << ',' << f.clientPredictTick << ',' << f.serverAckedClientTick << ','
        << f.reconcileErrorPosition << ',' << f.reconcileErrorVelocity << ',' << f.accumulatorMs << ','
        << f.measuredPhysicsHz << ',' << f.fpsCurrent << ',' << f.fps1pLow << ',' << f.fps5pLow << ','
        << f.playerEntities << ',' << f.localPlayers << ',' << f.renderableEntities << ',' << f.projectileEntities
        << ',' << f.fireFields << ',' << f.animatedCandidates << ',' << f.animatedSampled << ',' << f.animatedDrawn
        << ',' << f.skinnedInstances << ',' << f.boneMatrices << ',' << f.entityRenderCmds << ',' << f.pointLights
        << ',' << f.beamPointLights << ',' << f.impactParticles << ',' << f.tracerParticles << ',' << f.ribbonVertices
        << ',' << f.hitscanBeams << ',' << f.arcVertices << ',' << f.smokeParticles << ',' << f.decals << ','
        << f.audioSourcesActive << ',' << f.voiceSourcesActive << ',' << f.audioEventsPosted << ','
        << f.audioCommandsGenerated << ',' << f.audioSourcesStarted << ',' << f.audioDroppedByCooldown << ','
        << f.audioDroppedByLimit << ',' << f.audioStolenSources << ',' << f.rttMs << ',' << f.avgRttMs << ','
        << f.recvKBps << ',' << f.sendKBps << ',' << f.registryUpdateKB << ',' << f.swapchainWidth << ','
        << f.swapchainHeight << ',' << f.rendererWorldInstances << ',' << f.rendererEntityCmds << ','
        << f.rendererEntityDraws << ',' << f.rendererPointLights << ',' << f.rendererSkinnedInstances << ','
        << f.rendererWeaponDrawn << ',' << f.rendererModelDraws << ',' << f.rendererMeshDraws << ','
        << f.rendererIndexedDraws << ',' << f.rendererTriangles << ',' << f.rendererStaticBatchDraws << ','
        << f.rendererDynamicDraws << ',' << f.rendererMaterialBinds << ',' << f.rendererTextureBinds << ','
        << f.rendererStaticTriangles << ',' << f.rendererPresentMode << ',' << f.rendererFrameSubmitted << ','
        << f.rendererSwapchainSkipped << ',' << f.imguiDrawLists << ',' << f.imguiVertices << ',' << f.imguiIndices
        << ',' << f.perfMovementCalls << ',' << f.perfMovementPlayers << ',' << f.perfCollisionCalls << ','
        << f.perfCollisionPlayers << ',' << f.perfKccCalls << ',' << f.perfKccBumpHits << ',' << f.perfKccCaIterations
        << ',' << f.perfKccSweepHits << ',' << f.perfWallDetectCalls << ',' << f.perfWallMeshProbes << ','
        << f.perfWallMeshProbeMeshes << ',' << f.perfWallSphereFallbacks << ',' << f.perfWallAttachmentCalls << ','
        << f.perfWallAttachmentMeshes << ',' << f.perfWallDetectSkippedByGate << ','
        << f.perfWallAttachmentPrevTriangleHits << ',' << f.perfWallAttachmentNeighborHits << ','
        << f.perfWallAttachmentBroadphaseFallbacks << ',' << f.perfStaticBroadphaseQueries << ','
        << f.perfStaticBroadphaseMeshes << ',' << f.perfSweepAabbAllCalls << ',' << f.perfSweepCapsuleAllCalls << ','
        << f.perfSweepCapsuleTriMeshCalls << ',' << f.perfSweepCapsuleTriMeshNodes << ','
        << f.perfSweepCapsuleTriMeshTris << ',' << f.perfDeepestCapsuleCalls << ',' << f.perfDeepestCapsuleTriMeshCalls
        << ',' << f.perfDeepestCapsuleTriMeshNodes << ',' << f.perfDeepestCapsuleTriMeshTris << ','
        << f.perfClosestPointMeshCalls << ',' << f.perfClosestPointMeshNodes << ',' << f.perfClosestPointMeshTris << ','
        << f.perfClosestPointTriangleCalls << ',' << f.perfClosestPointWallProbeCalls << ','
        << f.perfClosestPointWallProbeNodes << ',' << f.perfClosestPointWallProbeTris << ','
        << f.perfClosestPointWallAttachmentCalls << ',' << f.perfClosestPointWallAttachmentNodes << ','
        << f.perfClosestPointWallAttachmentTris << '\n';
}

float maxSection(const ClientPerfFrame& f)
{
    return std::max({f.preambleMs,
                     f.inputMs,
                     f.networkStatsMs,
                     f.physicsMs,
                     f.networkPollMs,
                     f.snapshotApplyMs,
                     f.reconciliationMs,
                     f.refreshPlayersMs,
                     f.refreshProjectilesMs,
                     f.refreshRespawnsMs,
                     f.refreshDroppedWeaponsMs,
                     f.refreshPowerupsMs,
                     f.cameraResolveMs,
                     f.cameraMs,
                     f.localVfxMs,
                     f.dispatchMs,
                     f.particlesMs,
                     f.audioMs,
                     f.interpolationMs,
                     f.animationMs,
                     f.entityCmdsMs,
                     f.viewmodelMs,
                     f.recorderFpsMs,
                     f.imguiMs,
                     f.hudMs,
                     f.pauseMenuMs,
                     f.imguiRenderMs,
                     f.drawFrameMs,
                     f.frameLimiterMs});
}
} // namespace

void ClientPerfRecorder::configureFromEnv(const char* basePath)
{
    enabled_ = envEnabled(SDL_getenv("GROUP2_CLIENT_PERF"));
    if (!enabled_)
        return;

    reserveFrames_ = parseSizeEnv(SDL_getenv("GROUP2_CLIENT_PERF_RESERVE"), reserveFrames_);
    if (reserveFrames_ < 1024)
        reserveFrames_ = 1024;

    if (const char* dir = SDL_getenv("GROUP2_CLIENT_PERF_DIR"); dir && *dir) {
        baseDir_ = dir;
    } else {
        std::filesystem::path base = basePath ? basePath : "";
        base /= "perf";
        base /= "client";
        baseDir_ = base.string();
    }
}

void ClientPerfRecorder::start()
{
    if (!enabled_ || recording_)
        return;

    std::filesystem::path dir(baseDir_);
    dir /= makeTimestamp();
    try {
        std::filesystem::create_directories(dir);
    } catch (const std::exception& e) {
        SDL_Log("[client-perf] failed to create '%s': %s", dir.string().c_str(), e.what());
        enabled_ = false;
        return;
    }

    sessionDir_ = dir.string();
    frames_.clear();
    frames_.reserve(reserveFrames_);
    framesBeyondInitialReserve_ = 0;
    recording_ = true;
    SDL_Log("[client-perf] recording enabled: %s (reserve=%zu frames)", sessionDir_.c_str(), reserveFrames_);
}

void ClientPerfRecorder::stop()
{
    if (!recording_)
        return;
    recording_ = false;
    writeFramesCsv();
    writeSummary();
    SDL_Log("[client-perf] wrote %zu frames to %s", frames_.size(), sessionDir_.c_str());
    frames_.clear();
}

void ClientPerfRecorder::record(const ClientPerfFrame& frame)
{
    if (!recording_)
        return;
    if (frames_.size() >= reserveFrames_)
        ++framesBeyondInitialReserve_;
    frames_.push_back(frame);
}

void ClientPerfRecorder::writeFramesCsv() const
{
    const std::filesystem::path path = std::filesystem::path(sessionDir_) / "frames.csv";
    std::ofstream out(path);
    if (!out.is_open()) {
        SDL_Log("[client-perf] failed to open '%s'", path.string().c_str());
        return;
    }

    out << std::fixed << std::setprecision(4);
    writeHeader(out);
    for (const ClientPerfFrame& frame : frames_)
        writeFrame(out, frame);
}

void ClientPerfRecorder::writeSummary() const
{
    const std::filesystem::path path = std::filesystem::path(sessionDir_) / "summary.txt";
    std::ofstream out(path);
    if (!out.is_open()) {
        SDL_Log("[client-perf] failed to open '%s'", path.string().c_str());
        return;
    }

    std::vector<float> wallMs;
    std::vector<float> cpuMs;
    wallMs.reserve(frames_.size());
    cpuMs.reserve(frames_.size());
    for (const ClientPerfFrame& frame : frames_) {
        if (frame.wallFrameMs > 0.0f && frame.wallFrameMs < 10000.0f)
            wallMs.push_back(frame.wallFrameMs);
        if (frame.cpuFrameMs > 0.0f && frame.cpuFrameMs < 10000.0f)
            cpuMs.push_back(frame.cpuFrameMs);
    }
    std::sort(wallMs.begin(), wallMs.end());
    std::sort(cpuMs.begin(), cpuMs.end());

    auto fpsFromMs = [](float ms) { return ms > 0.0f ? 1000.0f / ms : 0.0f; };
    const float avgWall = average(wallMs);
    const float p50Wall = percentileMs(wallMs, 0.50f);
    const float p95Wall = percentileMs(wallMs, 0.95f);
    const float p99Wall = percentileMs(wallMs, 0.99f);
    const float maxWall = wallMs.empty() ? 0.0f : wallMs.back();
    const float avgCpu = average(cpuMs);

    out << std::fixed << std::setprecision(3);
    out << "frames=" << frames_.size() << '\n';
    out << "frames_beyond_initial_reserve=" << framesBeyondInitialReserve_ << '\n';
    out << "wall_ms_avg=" << avgWall << '\n';
    out << "wall_ms_p50=" << p50Wall << '\n';
    out << "wall_ms_p95=" << p95Wall << '\n';
    out << "wall_ms_p99=" << p99Wall << '\n';
    out << "wall_ms_max=" << maxWall << '\n';
    out << "fps_avg=" << fpsFromMs(avgWall) << '\n';
    out << "fps_p50=" << fpsFromMs(p50Wall) << '\n';
    out << "fps_p5=" << fpsFromMs(p95Wall) << '\n';
    out << "fps_p1=" << fpsFromMs(p99Wall) << '\n';
    out << "cpu_ms_avg=" << avgCpu << '\n';

    std::vector<const ClientPerfFrame*> slow;
    slow.reserve(frames_.size());
    for (const ClientPerfFrame& frame : frames_)
        slow.push_back(&frame);
    std::sort(slow.begin(), slow.end(), [](const ClientPerfFrame* a, const ClientPerfFrame* b) {
        return a->wallFrameMs > b->wallFrameMs;
    });

    out << "\nslowest_frames:\n";
    const std::size_t count = std::min<std::size_t>(slow.size(), 20);
    for (std::size_t i = 0; i < count; ++i) {
        const ClientPerfFrame& f = *slow[i];
        out << "frame=" << f.frameNumber << " wall_ms=" << f.wallFrameMs << " cpu_ms=" << f.cpuFrameMs
            << " max_section_ms=" << maxSection(f) << " draw_ms=" << f.drawFrameMs << " acquire_ms=" << f.drawAcquireMs
            << " record_ms=" << f.drawRecordMs << " submit_ms=" << f.drawSubmitMs
            << " swapchain_ms=" << f.rendererSwapchainAcquireMs << " geom_pass_ms=" << f.rendererGeometryPassMs
            << " weapon_pass_ms=" << f.rendererWeaponPassMs << " ui_pass_ms=" << f.rendererUiPassMs
            << " physics_ms=" << f.physicsMs << " poll_ms=" << f.networkPollMs
            << " snapshot_apply_ms=" << f.snapshotApplyMs << " reconcile_ms=" << f.reconciliationMs
            << " reconcile_ticks=" << f.reconcileReplayedTicks << " reconcile_skip=" << f.reconcileSkippedExact
            << " reconcile_err_pos=" << f.reconcileErrorPosition << " reconcile_err_vel=" << f.reconcileErrorVelocity
            << " refresh_players_ms=" << f.refreshPlayersMs << " animation_ms=" << f.animationMs
            << " hud_ms=" << f.hudMs << " imgui_ms=" << f.imguiMs << " limiter_ms=" << f.frameLimiterMs
            << " entity_cmds=" << f.entityRenderCmds << " skinned=" << f.skinnedInstances
            << " triangles=" << f.rendererTriangles << " static_batch_draws=" << f.rendererStaticBatchDraws
            << " dynamic_draws=" << f.rendererDynamicDraws << " submitted=" << f.rendererFrameSubmitted
            << " swapchain_skip=" << f.rendererSwapchainSkipped << " material_binds=" << f.rendererMaterialBinds
            << " texture_binds=" << f.rendererTextureBinds << " kcc=" << f.perfKccCalls
            << " sweep_capsule_tris=" << f.perfSweepCapsuleTriMeshTris
            << " closest_point_tris=" << f.perfClosestPointMeshTris << " wall_skip=" << f.perfWallDetectSkippedByGate
            << " wall_prev=" << f.perfWallAttachmentPrevTriangleHits
            << " wall_neighbor=" << f.perfWallAttachmentNeighborHits
            << " wall_broad=" << f.perfWallAttachmentBroadphaseFallbacks
            << " wall_probe_tris=" << f.perfClosestPointWallProbeTris
            << " wall_attach_tris=" << f.perfClosestPointWallAttachmentTris << '\n';
    }
}
