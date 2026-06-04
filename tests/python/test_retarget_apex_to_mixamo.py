#!/usr/bin/env python3
"""Tests for the Apex-to-Mixamo retargeting tool configuration layer."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import subprocess
import sys
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "tools" / "retarget_apex_to_mixamo.py"
CONFIG_PATH = REPO_ROOT / "tools" / "retarget_configs" / "r301_upperbody.json"


def load_tool_module():
    spec = importlib.util.spec_from_file_location("retarget_apex_to_mixamo", SCRIPT_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class RetargetConfigTests(unittest.TestCase):
    def test_r301_config_names_only_r301_outputs(self) -> None:
        self.assertTrue(SCRIPT_PATH.exists(), f"missing {SCRIPT_PATH}")
        self.assertTrue(CONFIG_PATH.exists(), f"missing {CONFIG_PATH}")

        config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))

        self.assertEqual(config["name"], "r301_upperbody")
        self.assertEqual(
            [clip["id"] for clip in config["clips"]],
            ["r301_idle_upper", "r301_reload_upper"],
        )
        self.assertTrue(all("kraber" not in clip["id"].lower() for clip in config["clips"]))
        self.assertTrue(all(clip["output_fbx"].endswith(".fbx") for clip in config["clips"]))

    def test_bone_map_covers_required_upper_body_controls(self) -> None:
        tool = load_tool_module()
        config = tool.load_config(CONFIG_PATH, REPO_ROOT)
        source_to_target = {entry.source: entry.target for entry in config.bone_map}

        expected = {
            "def_c_spineA": "mixamorig:Spine",
            "def_c_spineB": "mixamorig:Spine1",
            "def_c_spineC": "mixamorig:Spine2",
            "def_l_shoulder": "mixamorig:LeftArm",
            "def_l_elbow": "mixamorig:LeftForeArm",
            "def_l_wrist": "mixamorig:LeftHand",
            "def_r_shoulder": "mixamorig:RightArm",
            "def_r_elbow": "mixamorig:RightForeArm",
            "def_r_wrist": "mixamorig:RightHand",
        }

        for source, target in expected.items():
            self.assertEqual(source_to_target[source], target)

    def test_dry_run_reports_r301_clip_plan_without_blender(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(SCRIPT_PATH),
                "--config",
                str(CONFIG_PATH),
                "--repo-root",
                str(REPO_ROOT),
                "--dry-run",
            ],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )

        plan = json.loads(completed.stdout)
        self.assertEqual(plan["name"], "r301_upperbody")
        self.assertEqual(plan["target_rig"], "assets/animations/character_rigged_new.glb")
        self.assertEqual(len(plan["clips"]), 2)
        self.assertEqual(plan["clips"][0]["id"], "r301_idle_upper")
        self.assertEqual(plan["clips"][1]["id"], "r301_reload_upper")
        self.assertEqual(plan["bone_map_count"], 45)

    def test_blender_payload_uses_absolute_paths_for_batch_execution(self) -> None:
        tool = load_tool_module()
        config = tool.load_config(CONFIG_PATH, REPO_ROOT)

        payload = tool.build_blender_payload(config)

        self.assertEqual(payload["name"], "r301_upperbody")
        self.assertEqual(payload["target_rig"], str(REPO_ROOT / "assets/animations/character_rigged_new.glb"))
        self.assertEqual(
            payload["clips"][1]["output_fbx"],
            str(REPO_ROOT / "assets/animations/apex_retargeted/r301_reload_upper.fbx"),
        )
        self.assertEqual(payload["bone_map"][0], {"source": "def_c_hip", "target": "mixamorig:Hips"})
        self.assertIn("bpy.ops.export_scene.fbx", tool.build_blender_script(payload))

    def test_blender_script_uses_parent_relative_pose_deltas(self) -> None:
        tool = load_tool_module()
        config = tool.load_config(CONFIG_PATH, REPO_ROOT)

        script = tool.build_blender_script(tool.build_blender_payload(config))

        self.assertIn("def local_pose_matrix", script)
        self.assertIn("def pure_rotation_delta", script)
        self.assertIn("source_local_rest.inverted() @ source_local_pose", script)
        self.assertIn("delta.to_quaternion().to_matrix().to_4x4()", script)
        self.assertNotIn("source_rest[source_name].inverted() @ source_bone.matrix", script)


if __name__ == "__main__":
    unittest.main()
