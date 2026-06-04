#!/usr/bin/env python3
"""Batch retarget Apex upper-body GLB clips onto the project's Mixamo rig."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile
from dataclasses import dataclass


@dataclass(frozen=True)
class ClipPlan:
    id: str
    source: pathlib.Path
    output_fbx: pathlib.Path


@dataclass(frozen=True)
class BoneMapEntry:
    source: str
    target: str


@dataclass(frozen=True)
class RetargetConfig:
    name: str
    target_rig: pathlib.Path
    output_blend: pathlib.Path
    frame_step: int
    clips: list[ClipPlan]
    bone_map: list[BoneMapEntry]


def _resolve_repo_path(repo_root: pathlib.Path, raw: str) -> pathlib.Path:
    path = pathlib.Path(raw)
    return path if path.is_absolute() else repo_root / path


def _repo_relative(repo_root: pathlib.Path, path: pathlib.Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def load_config(config_path: pathlib.Path, repo_root: pathlib.Path) -> RetargetConfig:
    data = json.loads(pathlib.Path(config_path).read_text(encoding="utf-8"))

    required_top_level = {"name", "target_rig", "output_blend", "frame_step", "clips", "bone_map"}
    missing = sorted(required_top_level - set(data))
    if missing:
        raise ValueError(f"missing required config keys: {', '.join(missing)}")

    clips = [
        ClipPlan(
            id=str(item["id"]),
            source=_resolve_repo_path(repo_root, str(item["source"])),
            output_fbx=_resolve_repo_path(repo_root, str(item["output_fbx"])),
        )
        for item in data["clips"]
    ]
    bone_map = [BoneMapEntry(source=str(item["source"]), target=str(item["target"])) for item in data["bone_map"]]

    config = RetargetConfig(
        name=str(data["name"]),
        target_rig=_resolve_repo_path(repo_root, str(data["target_rig"])),
        output_blend=_resolve_repo_path(repo_root, str(data["output_blend"])),
        frame_step=int(data["frame_step"]),
        clips=clips,
        bone_map=bone_map,
    )
    validate_config(config)
    return config


def validate_config(config: RetargetConfig) -> None:
    if config.frame_step < 1:
        raise ValueError("frame_step must be >= 1")
    if not config.clips:
        raise ValueError("at least one clip is required")
    if not config.bone_map:
        raise ValueError("bone_map must not be empty")
    if len({clip.id for clip in config.clips}) != len(config.clips):
        raise ValueError("clip ids must be unique")
    if len({entry.target for entry in config.bone_map}) != len(config.bone_map):
        raise ValueError("bone_map target bones must be unique")

    paths = [config.target_rig, *(clip.source for clip in config.clips)]
    missing = [str(path) for path in paths if not path.exists()]
    if missing:
        raise FileNotFoundError("missing input asset(s): " + ", ".join(missing))


def build_dry_run_plan(config: RetargetConfig, repo_root: pathlib.Path) -> dict[str, object]:
    return {
        "name": config.name,
        "target_rig": _repo_relative(repo_root, config.target_rig),
        "output_blend": _repo_relative(repo_root, config.output_blend),
        "frame_step": config.frame_step,
        "bone_map_count": len(config.bone_map),
        "clips": [
            {
                "id": clip.id,
                "source": _repo_relative(repo_root, clip.source),
                "output_fbx": _repo_relative(repo_root, clip.output_fbx),
            }
            for clip in config.clips
        ],
    }


def build_blender_payload(config: RetargetConfig) -> dict[str, object]:
    return {
        "name": config.name,
        "target_rig": str(config.target_rig),
        "output_blend": str(config.output_blend),
        "frame_step": config.frame_step,
        "clips": [
            {"id": clip.id, "source": str(clip.source), "output_fbx": str(clip.output_fbx)} for clip in config.clips
        ],
        "bone_map": [{"source": entry.source, "target": entry.target} for entry in config.bone_map],
    }


def build_blender_script(payload: dict[str, object]) -> str:
    payload_json = json.dumps(payload, indent=2)
    return f"""
import json
import math
import pathlib

import bpy
from mathutils import Matrix


CONFIG = json.loads({payload_json!r})


def clear_scene():
    bpy.ops.object.mode_set(mode="OBJECT") if bpy.ops.object.mode_set.poll() else None
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def import_armature(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    imported = [obj for obj in bpy.data.objects if obj not in before]
    armatures = [obj for obj in imported if obj.type == "ARMATURE"]
    if not armatures:
        raise RuntimeError("No armature imported from " + path)
    return max(armatures, key=lambda obj: len(obj.data.bones))


def reset_pose(armature):
    for pose_bone in armature.pose.bones:
        pose_bone.rotation_mode = "QUATERNION"
        pose_bone.location = (0.0, 0.0, 0.0)
        pose_bone.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pose_bone.scale = (1.0, 1.0, 1.0)


def source_action(armature):
    if armature.animation_data and armature.animation_data.action:
        return armature.animation_data.action
    actions = list(bpy.data.actions)
    if actions:
        return actions[0]
    raise RuntimeError("Source armature has no action: " + armature.name)


def insert_pose_key(pose_bone, frame):
    pose_bone.keyframe_insert(data_path="location", frame=frame)
    pose_bone.keyframe_insert(data_path="rotation_quaternion", frame=frame)
    pose_bone.keyframe_insert(data_path="scale", frame=frame)


def local_rest_matrix(armature, bone_name):
    bone = armature.data.bones[bone_name]
    if bone.parent:
        return bone.parent.matrix_local.inverted() @ bone.matrix_local
    return bone.matrix_local.copy()


def local_pose_matrix(pose_bone):
    if pose_bone.parent:
        return pose_bone.parent.matrix.inverted() @ pose_bone.matrix
    return pose_bone.matrix.copy()


def pure_rotation_delta(delta):
    return delta.to_quaternion().to_matrix().to_4x4()


def retarget_clip(clip):
    clear_scene()
    source = import_armature(clip["source"])
    target = import_armature(CONFIG["target_rig"])
    source.name = "SRC_" + clip["id"]
    target.name = "TGT_" + clip["id"]

    reset_pose(source)
    reset_pose(target)

    action = source_action(source)
    start = int(math.floor(action.frame_range[0]))
    end = int(math.ceil(action.frame_range[1]))
    scene = bpy.context.scene
    scene.frame_start = start
    scene.frame_end = end
    scene.render.fps = 30

    target.animation_data_clear()
    target.animation_data_create()
    baked = bpy.data.actions.new(clip["id"])
    target.animation_data.action = baked

    mapping = [(entry["source"], entry["target"]) for entry in CONFIG["bone_map"]]

    missing_source = [name for name, _target_name in mapping if name not in source.pose.bones]
    missing_target = [name for _source_name, name in mapping if name not in target.pose.bones]
    if missing_source or missing_target:
        raise RuntimeError(
            "Missing mapped bones for " + clip["id"] +
            ": source=" + ", ".join(missing_source) +
            " target=" + ", ".join(missing_target)
        )

    for frame in range(start, end + 1, int(CONFIG["frame_step"])):
        scene.frame_set(frame)
        bpy.context.view_layer.update()
        for source_name, target_name in mapping:
            source_bone = source.pose.bones[source_name]
            target_bone = target.pose.bones[target_name]
            source_local_rest = local_rest_matrix(source, source_name)
            source_local_pose = local_pose_matrix(source_bone)
            delta = source_local_rest.inverted() @ source_local_pose
            delta = pure_rotation_delta(delta)
            target_local_pose = local_rest_matrix(target, target_name) @ delta
            if target_bone.parent:
                target_bone.matrix = target_bone.parent.matrix @ target_local_pose
            else:
                target_bone.matrix = target_local_pose
            insert_pose_key(target_bone, frame)

    output = pathlib.Path(clip["output_fbx"])
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    bpy.ops.export_scene.fbx(
        filepath=str(output),
        use_selection=True,
        object_types={{"ARMATURE"}},
        add_leaf_bones=False,
        bake_anim=True,
        bake_anim_use_all_actions=False,
        bake_anim_use_nla_strips=False,
        bake_anim_force_startend_keying=True,
        bake_anim_step=1.0,
        bake_anim_simplify_factor=0.0,
    )
    return {{"id": clip["id"], "frames": [start, end], "output_fbx": str(output), "action": action.name}}


results = []
for clip_config in CONFIG["clips"]:
    results.append(retarget_clip(clip_config))

output_blend = pathlib.Path(CONFIG["output_blend"])
output_blend.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(output_blend))

result = {{"name": CONFIG["name"], "clips": results, "output_blend": str(output_blend)}}
print(json.dumps(result, indent=2))
"""


def execute_with_blender(config: RetargetConfig, blender: str) -> int:
    payload = build_blender_payload(config)
    script = build_blender_script(payload)
    with tempfile.NamedTemporaryFile("w", suffix="_retarget_apex.py", encoding="utf-8", delete=False) as handle:
        handle.write(script)
        script_path = pathlib.Path(handle.name)
    try:
        completed = subprocess.run(
            [blender, "--background", "--factory-startup", "--python", str(script_path)],
            text=True,
            check=False,
        )
        return completed.returncode
    finally:
        script_path.unlink(missing_ok=True)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=pathlib.Path, required=True)
    parser.add_argument("--repo-root", type=pathlib.Path, default=pathlib.Path.cwd())
    parser.add_argument("--dry-run", action="store_true", help="Validate config and print the planned clips as JSON.")
    parser.add_argument("--print-blender-script", action="store_true", help="Print the generated Blender script.")
    parser.add_argument("--execute", action="store_true", help="Run Blender and export the retargeted FBX clips.")
    parser.add_argument("--blender", default="blender", help="Blender executable to use with --execute.")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(list(sys.argv[1:] if argv is None else argv))
    repo_root = args.repo_root.resolve()
    config = load_config(args.config, repo_root)

    if args.dry_run:
        print(json.dumps(build_dry_run_plan(config, repo_root), indent=2))
        return 0

    if args.print_blender_script:
        print(build_blender_script(build_blender_payload(config)))
        return 0

    if args.execute:
        return execute_with_blender(config, args.blender)

    raise SystemExit("Choose --dry-run, --print-blender-script, or --execute.")


if __name__ == "__main__":
    raise SystemExit(main())
