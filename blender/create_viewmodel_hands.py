"""Create first-person hand meshes and gun grip mount empties from new.blend.

Run with:
    blender --background blender/new.blend --python blender/create_viewmodel_hands.py

The script is intentionally deterministic: it removes previous generated hand
objects/mounts, recreates them from Beta_Surface vertex groups, exports compact
GLB hand assets, and saves the updated blend file.
"""

from __future__ import annotations

import json
from pathlib import Path

import bpy
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[1]
ASSETS_DIR = ROOT / "assets"

PLAYER_MESH = "Beta_Surface"
ARMATURE = "Armature"
GUN_MESHES = ("body ", "orange ")

GENERATED_OBJECTS = (
    "VM_LeftHand",
    "VM_RightHand",
    "gun_mount_right_hand",
    "gun_mount_left_hand",
    "gun_mount_muzzle",
)

SIDE_CONFIG = {
    "Left": {
        "object_name": "VM_LeftHand",
        "asset": ASSETS_DIR / "viewmodel_hand_left.glb",
        "anchor_bone": "mixamorig:LeftHand",
        "groups": (
            "mixamorig:LeftShoulder",
            "mixamorig:LeftArm",
            "mixamorig:LeftForeArm",
            "mixamorig:LeftHand",
            "mixamorig:LeftHandThumb",
            "mixamorig:LeftHandIndex",
            "mixamorig:LeftHandMiddle",
            "mixamorig:LeftHandRing",
            "mixamorig:LeftHandPinky",
        ),
    },
    "Right": {
        "object_name": "VM_RightHand",
        "asset": ASSETS_DIR / "viewmodel_hand_right.glb",
        "anchor_bone": "mixamorig:RightHand",
        "groups": (
            "mixamorig:RightShoulder",
            "mixamorig:RightArm",
            "mixamorig:RightForeArm",
            "mixamorig:RightHand",
            "mixamorig:RightHandThumb",
            "mixamorig:RightHandIndex",
            "mixamorig:RightHandMiddle",
            "mixamorig:RightHandRing",
            "mixamorig:RightHandPinky",
        ),
    },
}


def remove_previous_generated() -> None:
    for name in GENERATED_OBJECTS:
        obj = bpy.data.objects.get(name)
        if obj is not None:
            bpy.data.objects.remove(obj, do_unlink=True)


def group_matches(name: str, prefixes: tuple[str, ...]) -> bool:
    return any(name == prefix or name.startswith(prefix) for prefix in prefixes)


def make_hand_object(side: str) -> bpy.types.Object:
    source = bpy.data.objects.get(PLAYER_MESH)
    armature = bpy.data.objects.get(ARMATURE)
    if source is None or source.type != "MESH":
        raise RuntimeError(f"Missing source mesh {PLAYER_MESH!r}")
    if armature is None or armature.type != "ARMATURE":
        raise RuntimeError(f"Missing armature {ARMATURE!r}")

    config = SIDE_CONFIG[side]
    prefixes = config["groups"]
    group_ids = {
        group.index
        for group in source.vertex_groups
        if group_matches(group.name, prefixes)
    }
    if not group_ids:
        raise RuntimeError(f"No matching vertex groups for {side}")

    keep_vertices: set[int] = set()
    for vertex in source.data.vertices:
        if any(weight.group in group_ids and weight.weight > 0.035 for weight in vertex.groups):
            keep_vertices.add(vertex.index)

    if not keep_vertices:
        raise RuntimeError(f"No vertices selected for {side}")

    hand = source.copy()
    hand.data = source.data.copy()
    hand.animation_data_clear()
    hand.name = config["object_name"]
    hand.data.name = f"{config['object_name']}_Mesh"
    hand.parent = None
    for modifier in list(hand.modifiers):
        hand.modifiers.remove(modifier)
    bpy.context.collection.objects.link(hand)

    hand.matrix_world = source.matrix_world.copy()
    bpy.ops.object.select_all(action="DESELECT")
    hand.select_set(True)
    bpy.context.view_layer.objects.active = hand

    for vertex in hand.data.vertices:
        vertex.select = vertex.index not in keep_vertices
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_mode(type="VERT")
    bpy.ops.mesh.delete(type="VERT")
    bpy.ops.object.mode_set(mode="OBJECT")

    anchor_bone = armature.data.bones.get(config["anchor_bone"])
    if anchor_bone is None:
        raise RuntimeError(f"Missing anchor bone {config['anchor_bone']!r}")
    anchor_world = armature.matrix_world @ anchor_bone.head_local

    # Bake the source transform into vertex positions and move the wrist/hand
    # joint to local origin. The runtime can then place the hand directly at a
    # weapon grip target without inheriting the full body offset.
    world = hand.matrix_world.copy()
    for vertex in hand.data.vertices:
        vertex.co = world @ vertex.co
        vertex.co -= anchor_world
    hand.matrix_world.identity()
    hand.vertex_groups.clear()

    return hand


def world_bounds(objects: tuple[bpy.types.Object, ...]) -> tuple[Vector, Vector]:
    points: list[Vector] = []
    for obj in objects:
        points.extend(obj.matrix_world @ Vector(corner) for corner in obj.bound_box)
    min_v = Vector((min(p.x for p in points), min(p.y for p in points), min(p.z for p in points)))
    max_v = Vector((max(p.x for p in points), max(p.y for p in points), max(p.z for p in points)))
    return min_v, max_v


def add_gun_mounts() -> dict[str, list[float]]:
    gun_objects = tuple(obj for name in GUN_MESHES if (obj := bpy.data.objects.get(name)) is not None)
    if not gun_objects:
        raise RuntimeError("No gun meshes found for mount placement")

    min_v, max_v = world_bounds(gun_objects)
    center_x = (min_v.x + max_v.x) * 0.5
    center_z = (min_v.z + max_v.z) * 0.5

    mounts = {
        "gun_mount_right_hand": Vector((center_x, min_v.y + 1.15, min_v.z + 0.35)),
        "gun_mount_left_hand": Vector((center_x, min_v.y + 2.35, min_v.z + 0.46)),
        "gun_mount_muzzle": Vector((center_x, max_v.y - 0.03, center_z)),
    }

    parent = gun_objects[0]
    authored: dict[str, list[float]] = {}
    for name, position in mounts.items():
        empty = bpy.data.objects.new(name, None)
        empty.empty_display_type = "ARROWS"
        empty.empty_display_size = 0.18
        bpy.context.collection.objects.link(empty)
        empty.parent = parent
        empty.matrix_parent_inverse = parent.matrix_world.inverted()
        empty.matrix_world.translation = position
        authored[name] = [round(position.x, 6), round(position.y, 6), round(position.z, 6)]
    return authored


def export_hand(hand: bpy.types.Object, filepath: Path) -> None:
    filepath.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    hand.select_set(True)
    bpy.context.view_layer.objects.active = hand
    bpy.ops.export_scene.gltf(
        filepath=str(filepath),
        export_format="GLB",
        use_selection=True,
        export_materials="EXPORT",
        export_animations=False,
        export_apply=True,
    )


def main() -> None:
    remove_previous_generated()
    left = make_hand_object("Left")
    right = make_hand_object("Right")
    mounts = add_gun_mounts()

    export_hand(left, SIDE_CONFIG["Left"]["asset"])
    export_hand(right, SIDE_CONFIG["Right"]["asset"])

    manifest_path = ASSETS_DIR / "viewmodel_hand_mounts.json"
    manifest_path.write_text(
        json.dumps(
            {
                "source_blend": str(Path(bpy.data.filepath).resolve()),
                "hands": {
                    "left": SIDE_CONFIG["Left"]["asset"].name,
                    "right": SIDE_CONFIG["Right"]["asset"].name,
                },
                "authored_mounts_world": mounts,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )

    bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)


if __name__ == "__main__":
    main()
