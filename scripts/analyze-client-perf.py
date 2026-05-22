#!/usr/bin/env python3
"""Summarize GROUP2_CLIENT_PERF=1 client frame captures."""

from __future__ import annotations

import argparse
import csv
import pathlib
import statistics
from typing import Iterable


SECTION_COLUMNS = [
    "preamble_ms",
    "input_ms",
    "network_stats_ms",
    "physics_ms",
    "network_poll_ms",
    "snapshot_apply_ms",
    "reconciliation_ms",
    "refresh_players_ms",
    "refresh_projectiles_ms",
    "refresh_respawns_ms",
    "refresh_dropped_weapons_ms",
    "refresh_powerups_ms",
    "camera_resolve_ms",
    "camera_ms",
    "local_vfx_ms",
    "dispatch_ms",
    "particles_ms",
    "audio_ms",
    "interpolation_ms",
    "animation_ms",
    "entity_cmds_ms",
    "viewmodel_ms",
    "recorder_fps_ms",
    "imgui_ms",
    "hud_ms",
    "pause_menu_ms",
    "imgui_render_ms",
    "draw_frame_ms",
    "frame_limiter_ms",
]

COUNTER_COLUMNS = [
    "snapshot_apply_count",
    "snapshot_applied",
    "reconcile_requested_ticks",
    "reconcile_replayed_ticks",
    "reconcile_missing_ticks",
    "reconcile_skipped_exact",
    "reconcile_replay_forced",
    "reconcile_missing_history",
    "perf_movement_calls",
    "perf_movement_players",
    "perf_collision_calls",
    "perf_collision_players",
    "perf_kcc_calls",
    "perf_kcc_bump_hits",
    "perf_kcc_ca_iterations",
    "perf_kcc_sweep_hits",
    "perf_wall_detect_calls",
    "perf_wall_mesh_probes",
    "perf_wall_mesh_probe_meshes",
    "perf_wall_sphere_fallbacks",
    "perf_wall_attachment_calls",
    "perf_wall_attachment_meshes",
    "perf_wall_detect_skipped_by_gate",
    "perf_wall_attachment_prev_triangle_hits",
    "perf_wall_attachment_neighbor_hits",
    "perf_wall_attachment_broadphase_fallbacks",
    "perf_static_broadphase_queries",
    "perf_static_broadphase_meshes",
    "perf_sweep_capsule_all_calls",
    "perf_sweep_capsule_trimesh_calls",
    "perf_sweep_capsule_trimesh_nodes",
    "perf_sweep_capsule_trimesh_tris",
    "perf_deepest_capsule_calls",
    "perf_deepest_capsule_trimesh_calls",
    "perf_deepest_capsule_trimesh_nodes",
    "perf_deepest_capsule_trimesh_tris",
    "perf_closest_point_mesh_calls",
    "perf_closest_point_mesh_nodes",
    "perf_closest_point_mesh_tris",
    "perf_closest_point_triangle_calls",
    "perf_closest_point_wall_probe_calls",
    "perf_closest_point_wall_probe_nodes",
    "perf_closest_point_wall_probe_tris",
    "perf_closest_point_wall_attachment_calls",
    "perf_closest_point_wall_attachment_nodes",
    "perf_closest_point_wall_attachment_tris",
]


def pct(sorted_values: list[float], p: float) -> float:
    if not sorted_values:
        return 0.0
    return sorted_values[int((len(sorted_values) - 1) * p)]


def avg(values: Iterable[float]) -> float:
    values = list(values)
    return statistics.fmean(values) if values else 0.0


def fps(ms: float) -> float:
    return 1000.0 / ms if ms > 0.0 else 0.0


def load_rows(path: pathlib.Path) -> list[dict[str, str]]:
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def f(row: dict[str, str], key: str) -> float:
    try:
        return float(row.get(key, "0") or 0.0)
    except ValueError:
        return 0.0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", help="Capture directory or frames.csv path")
    parser.add_argument("--top", type=int, default=20, help="Number of slow frames to print")
    args = parser.parse_args()

    path = pathlib.Path(args.capture)
    csv_path = path / "frames.csv" if path.is_dir() else path
    rows = load_rows(csv_path)
    if not rows:
        print(f"no rows in {csv_path}")
        return 1

    wall = sorted(f(r, "wall_frame_ms") for r in rows if 0.0 < f(r, "wall_frame_ms") < 10000.0)
    cpu = sorted(f(r, "cpu_frame_ms") for r in rows if 0.0 < f(r, "cpu_frame_ms") < 10000.0)
    print(f"frames: {len(rows)}")
    print(
        "wall ms: "
        f"avg={avg(wall):.3f} p50={pct(wall, 0.50):.3f} "
        f"p95={pct(wall, 0.95):.3f} p99={pct(wall, 0.99):.3f} max={wall[-1]:.3f}"
    )
    print(
        "fps:     "
        f"avg={fps(avg(wall)):.1f} p50={fps(pct(wall, 0.50)):.1f} "
        f"p5={fps(pct(wall, 0.95)):.1f} p1={fps(pct(wall, 0.99)):.1f} min={fps(wall[-1]):.1f}"
    )
    print(f"cpu ms:  avg={avg(cpu):.3f} p95={pct(cpu, 0.95):.3f} p99={pct(cpu, 0.99):.3f}")

    slow_cutoff = pct(wall, 0.99)
    slow_rows = [r for r in rows if f(r, "wall_frame_ms") >= slow_cutoff]
    print("\nslowest 1% avg section ms:")
    section_avgs = [(name, avg(f(r, name) for r in slow_rows)) for name in SECTION_COLUMNS]
    for name, value in sorted(section_avgs, key=lambda item: item[1], reverse=True)[:16]:
        print(f"  {name:20s} {value:8.3f}")

    print("\nphysics tick distribution:")
    for tick_value in sorted({int(f(r, "physics_ticks")) for r in rows}):
        tick_rows = [r for r in rows if int(f(r, "physics_ticks")) == tick_value]
        print(
            f"  ticks={tick_value:<2d} n={len(tick_rows):6d} "
            f"wall_avg={avg(f(r, 'wall_frame_ms') for r in tick_rows):7.3f} "
            f"phys_avg={avg(f(r, 'physics_ms') for r in tick_rows):7.3f} "
            f"poll_avg={avg(f(r, 'network_poll_ms') for r in tick_rows):7.3f} "
            f"snap_avg={avg(f(r, 'snapshot_apply_ms') for r in tick_rows):7.3f} "
            f"rec_avg={avg(f(r, 'reconciliation_ms') for r in tick_rows):7.3f} "
            f"draw_avg={avg(f(r, 'draw_frame_ms') for r in tick_rows):7.3f}"
        )

    print("\nslowest 1% avg workload counters:")
    counter_avgs = [(name, avg(f(r, name) for r in slow_rows)) for name in COUNTER_COLUMNS]
    for name, value in sorted(counter_avgs, key=lambda item: item[1], reverse=True)[:18]:
        if value > 0.0:
            print(f"  {name:34s} {value:10.2f}")

    print("\ntop slow frames:")
    for row in sorted(rows, key=lambda r: f(r, "wall_frame_ms"), reverse=True)[: args.top]:
        sections = [(name, f(row, name)) for name in SECTION_COLUMNS]
        dominant, dominant_ms = max(sections, key=lambda item: item[1])
        print(
            f"  frame={row['frame']} wall={f(row, 'wall_frame_ms'):.3f}ms "
            f"cpu={f(row, 'cpu_frame_ms'):.3f}ms dom={dominant}:{dominant_ms:.3f}ms "
            f"phys={f(row, 'physics_ms'):.3f}ms poll={f(row, 'network_poll_ms'):.3f}ms "
            f"snap={f(row, 'snapshot_apply_ms'):.3f}ms rec={f(row, 'reconciliation_ms'):.3f}ms "
            f"recSkip={row.get('reconcile_skipped_exact', '0')} "
            f"recErrP={f(row, 'reconcile_error_position'):.3f} "
            f"recErrV={f(row, 'reconcile_error_velocity'):.3f} "
            f"refreshP={f(row, 'refresh_players_ms'):.3f}ms cam={f(row, 'camera_resolve_ms'):.3f}ms "
            f"draw={f(row, 'draw_frame_ms'):.3f}ms acq={f(row, 'draw_acquire_ms'):.3f}ms "
            f"phys_ticks={row.get('physics_ticks', '0')} rec_ticks={row.get('reconcile_replayed_ticks', '0')} "
            f"kcc={row.get('perf_kcc_calls', '0')} sweepTri={row.get('perf_sweep_capsule_trimesh_tris', '0')} "
            f"closestTri={row.get('perf_closest_point_mesh_tris', '0')} "
            f"wallSkip={row.get('perf_wall_detect_skipped_by_gate', '0')} "
            f"wallPrev={row.get('perf_wall_attachment_prev_triangle_hits', '0')} "
            f"wallNbr={row.get('perf_wall_attachment_neighbor_hits', '0')} "
            f"wallBroad={row.get('perf_wall_attachment_broadphase_fallbacks', '0')} "
            f"wallProbeTri={row.get('perf_closest_point_wall_probe_tris', '0')} "
            f"wallAttachTri={row.get('perf_closest_point_wall_attachment_tris', '0')} "
            f"draws={row.get('renderer_mesh_draws', '0')} tris={row.get('renderer_triangles', '0')}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
