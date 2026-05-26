"""Create first-person hand meshes and rifle grip mount empties from new.blend.

Run with:
    blender --background blender/new.blend --python blender/create_viewmodel_hands.py

The script is intentionally deterministic: it removes previous generated hand
objects/mounts, recreates them from Beta_Surface vertex groups, exports compact
GLB hand assets, exports the rifle with authored mount points, and saves the
updated blend file.
"""

from __future__ import annotations

import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Vector


ROOT = Path(__file__).resolve().parents[1]
ASSETS_DIR = ROOT / "assets"

PLAYER_MESH = "Beta_Surface"
ARMATURE = "Armature"
GUN_MESHES = ("body ", "orange ")
RIFLE_ASSET = ASSETS_DIR / "assault_rifle_with_mountpoints.glb"
DEBUG_DOT_ASSET = ASSETS_DIR / "debug_red_dot.glb"
MOUNT_COLLECTION = "WEAPON_MOUNTPOINTS_RIFLE"

GENERATED_OBJECTS = (
    "VM_LeftHand",
    "VM_RightHand",
    "VM_LeftHand_Armature",
    "VM_RightHand_Armature",
    "gun_mount_right_hand",
    "gun_mount_left_hand",
    "gun_mount_muzzle",
    "rifle_mountpoints_root",
    "debug_red_dot",
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
            remove_object_tree(obj)


def remove_object_tree(obj: bpy.types.Object) -> None:
    for child in list(obj.children):
        remove_object_tree(child)
    bpy.data.objects.remove(obj, do_unlink=True)


def group_matches(name: str, prefixes: tuple[str, ...]) -> bool:
    return any(name == prefix or name.startswith(prefix) for prefix in prefixes)


def make_hand_object(side: str) -> tuple[bpy.types.Object, bpy.types.Object]:
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

    anchor_bone = armature.data.bones.get(config["anchor_bone"])
    if anchor_bone is None:
        raise RuntimeError(f"Missing anchor bone {config['anchor_bone']!r}")
    anchor_world = armature.matrix_world @ anchor_bone.head_local

    # Build a compact hand mesh directly instead of using edit-mode operators;
    # MCP/background contexts do not always provide an active 3D view.  Vertices
    # are baked into wrist-local space while their skin weights are preserved.
    source_mesh = source.data
    kept_polygons = [poly for poly in source_mesh.polygons if all(index in keep_vertices for index in poly.vertices)]
    used_vertices = sorted({index for poly in kept_polygons for index in poly.vertices})
    if not used_vertices or not kept_polygons:
        raise RuntimeError(f"No polygons selected for {side}")

    remap = {old: new for new, old in enumerate(used_vertices)}
    vertices = [source.matrix_world @ source_mesh.vertices[index].co - anchor_world for index in used_vertices]
    faces = [[remap[index] for index in poly.vertices] for poly in kept_polygons]

    hand_mesh = bpy.data.meshes.new(f"{config['object_name']}_Mesh")
    hand_mesh.from_pydata([tuple(v) for v in vertices], [], faces)
    hand_mesh.update()
    for material in source_mesh.materials:
        hand_mesh.materials.append(material)
    for new_poly, source_poly in zip(hand_mesh.polygons, kept_polygons):
        new_poly.material_index = source_poly.material_index

    hand = bpy.data.objects.new(config["object_name"], hand_mesh)
    bpy.context.collection.objects.link(hand)
    for group in source.vertex_groups:
        hand.vertex_groups.new(name=group.name)
    for old_index, new_index in remap.items():
        source_vertex = source_mesh.vertices[old_index]
        for weight in source_vertex.groups:
            hand.vertex_groups[weight.group].add([new_index], weight.weight, "REPLACE")

    hand_armature = armature.copy()
    hand_armature.data = armature.data.copy()
    hand_armature.name = f"{config['object_name']}_Armature"
    hand_armature.data.name = f"{config['object_name']}_ArmatureData"
    hand_armature.animation_data_clear()
    hand_armature.matrix_world = Matrix.Translation(-anchor_world) @ armature.matrix_world
    bpy.context.collection.objects.link(hand_armature)

    hand.parent = hand_armature
    hand.matrix_parent_inverse = hand_armature.matrix_world.inverted()

    armature_mod = hand.modifiers.new("ViewmodelArmature", "ARMATURE")
    armature_mod.object = hand_armature

    return hand, hand_armature


def world_bounds(objects: tuple[bpy.types.Object, ...]) -> tuple[Vector, Vector]:
    points: list[Vector] = []
    for obj in objects:
        points.extend(obj.matrix_world @ Vector(corner) for corner in obj.bound_box)
    min_v = Vector((min(p.x for p in points), min(p.y for p in points), min(p.z for p in points)))
    max_v = Vector((max(p.x for p in points), max(p.y for p in points), max(p.z for p in points)))
    return min_v, max_v


def make_mount_empty(
    name: str,
    world_position: Vector,
    parent: bpy.types.Object,
    collection: bpy.types.Collection,
    rotation: tuple[float, float, float] = (0.0, 0.0, 0.0),
) -> bpy.types.Object:
    empty = bpy.data.objects.new(name, None)
    empty.empty_display_type = "SPHERE" if name.startswith("ik_") else "ARROWS"
    empty.empty_display_size = 0.07
    collection.objects.link(empty)
    empty.parent = parent
    empty.location = parent.matrix_world.inverted() @ world_position
    empty.rotation_euler = rotation
    return empty


def add_rifle_mounts() -> dict[str, list[float]]:
    gun_objects = tuple(obj for name in GUN_MESHES if (obj := bpy.data.objects.get(name)) is not None)
    if not gun_objects:
        raise RuntimeError("No gun meshes found for mount placement")

    min_v, max_v = world_bounds(gun_objects)
    center_x = (min_v.x + max_v.x) * 0.5
    center_z = (min_v.z + max_v.z) * 0.5

    root = bpy.data.objects.get("rifle_mountpoints_root")
    if root is not None:
        remove_object_tree(root)

    collection = bpy.data.collections.get(MOUNT_COLLECTION)
    if collection is None:
        collection = bpy.data.collections.new(MOUNT_COLLECTION)
        bpy.context.scene.collection.children.link(collection)

    body = gun_objects[0]
    root = bpy.data.objects.new("rifle_mountpoints_root", None)
    root.empty_display_type = "ARROWS"
    root.empty_display_size = 0.12
    collection.objects.link(root)
    root.parent = body
    root.location = Vector((0.0, 0.0, 0.0))
    root.rotation_euler = (0.0, 0.0, 0.0)
    bpy.context.view_layer.update()

    right_side = center_x + 0.13
    left_side = center_x - 0.13
    far_right = center_x + 0.52
    far_left = center_x - 0.52
    grip_y = min_v.y + 1.20
    trigger_y = min_v.y + 1.36
    mag_y = min_v.y + 2.07
    front_mag_y = min_v.y + 2.26
    rear_mag_y = min_v.y + 1.94
    grip_z = min_v.z + 0.32
    low_grip_z = min_v.z + 0.22
    mag_z = min_v.z + 0.30
    high_contact_z = min_v.z + 0.43

    # Blender axes: X = weapon right, Y = weapon length, Z = weapon up.
    # These points are authored on contact surfaces: right hand wraps the rear
    # pistol grip/trigger area; left support hand wraps the magazine well.
    mounts = {
        "socket_muzzle": Vector((center_x, max_v.y - 0.03, center_z)),
        "is_muzzle": Vector((center_x, max_v.y - 0.03, center_z)),
        "socket_ads": Vector((center_x, min_v.y + 1.90, max_v.z + 0.03)),
        "socket_trigger": Vector((center_x + 0.03, trigger_y, grip_z + 0.02)),
        "socket_mag": Vector((center_x, mag_y, mag_z)),
        "ik_r_palm": Vector((right_side, grip_y - 0.06, grip_z + 0.03)),
        "ik_r_elbow": Vector((far_right, grip_y - 0.42, grip_z - 0.08)),
        "ik_r_thumb_tip": Vector((left_side + 0.03, grip_y - 0.10, high_contact_z)),
        "ik_r_index_tip": Vector((left_side, trigger_y + 0.03, grip_z + 0.06)),
        "ik_r_middle_tip": Vector((left_side - 0.01, grip_y - 0.02, grip_z - 0.02)),
        "ik_r_ring_tip": Vector((left_side, grip_y - 0.11, low_grip_z + 0.03)),
        "ik_r_pinky_tip": Vector((left_side + 0.01, grip_y - 0.20, low_grip_z)),
        "ik_l_palm": Vector((left_side, mag_y, mag_z + 0.04)),
        "ik_l_elbow": Vector((far_left, mag_y - 0.22, mag_z - 0.05)),
        "ik_l_thumb_tip": Vector((right_side - 0.03, rear_mag_y, high_contact_z - 0.02)),
        "ik_l_index_tip": Vector((right_side, front_mag_y, mag_z + 0.02)),
        "ik_l_middle_tip": Vector((right_side + 0.01, mag_y + 0.06, mag_z - 0.04)),
        "ik_l_ring_tip": Vector((right_side, mag_y - 0.05, mag_z - 0.07)),
        "ik_l_pinky_tip": Vector((right_side - 0.01, mag_y - 0.15, mag_z - 0.08)),
    }

    authored: dict[str, list[float]] = {}
    for name, position in mounts.items():
        empty = make_mount_empty(name, position, root, collection)
        if name == "is_muzzle":
            empty["is_muzzle"] = True
        authored[name] = [round(position.x, 6), round(position.y, 6), round(position.z, 6)]
    return authored


def export_hand(hand: bpy.types.Object, armature: bpy.types.Object, filepath: Path) -> None:
    filepath.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    hand.select_set(True)
    armature.select_set(True)
    bpy.context.view_layer.objects.active = hand
    bpy.ops.export_scene.gltf(
        filepath=str(filepath),
        export_format="GLB",
        use_selection=True,
        export_materials="EXPORT",
        export_animations=False,
        export_apply=False,
    )


def export_rifle(filepath: Path) -> None:
    filepath.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    selected: list[bpy.types.Object] = []
    for name in GUN_MESHES + ("rifle_mountpoints_root",):
        obj = bpy.data.objects.get(name)
        if obj is None:
            continue
        selected.append(obj)
        selected.extend(obj.children_recursive)
    for obj in selected:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = bpy.data.objects.get("body ")
    bpy.ops.export_scene.gltf(
        filepath=str(filepath),
        export_format="GLB",
        use_selection=True,
        export_materials="EXPORT",
        export_animations=False,
        export_apply=True,
        export_extras=True,
    )


def export_debug_dot(filepath: Path) -> None:
    obj = bpy.data.objects.get("debug_red_dot")
    if obj is not None:
        bpy.data.objects.remove(obj, do_unlink=True)

    mat = bpy.data.materials.get("debug_red_dot_red") or bpy.data.materials.new("debug_red_dot_red")
    mat.diffuse_color = (1.0, 0.02, 0.02, 1.0)
    if mat.use_nodes:
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf is not None:
            bsdf.inputs["Base Color"].default_value = (1.0, 0.02, 0.02, 1.0)
            bsdf.inputs["Roughness"].default_value = 0.35
    else:
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf is not None:
            bsdf.inputs["Base Color"].default_value = (1.0, 0.02, 0.02, 1.0)
            bsdf.inputs["Roughness"].default_value = 0.35

    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=1.0, location=(0.0, 0.0, 0.0))
    obj = bpy.context.object
    obj.name = "debug_red_dot"
    obj.data.name = "debug_red_dot_mesh"
    obj.data.materials.append(mat)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(
        filepath=str(filepath),
        export_format="GLB",
        use_selection=True,
        export_materials="EXPORT",
        export_animations=False,
        export_apply=True,
    )
    bpy.data.objects.remove(obj, do_unlink=True)


def main() -> None:
    remove_previous_generated()
    left, left_armature = make_hand_object("Left")
    right, right_armature = make_hand_object("Right")
    mounts = add_rifle_mounts()

    export_hand(left, left_armature, SIDE_CONFIG["Left"]["asset"])
    export_hand(right, right_armature, SIDE_CONFIG["Right"]["asset"])
    export_rifle(RIFLE_ASSET)
    export_debug_dot(DEBUG_DOT_ASSET)

    manifest_path = ASSETS_DIR / "viewmodel_hand_mounts.json"
    manifest_path.write_text(
        json.dumps(
            {
                "source_blend": str(Path(bpy.data.filepath).resolve()),
                "rifle": RIFLE_ASSET.name,
                "debug_dot": DEBUG_DOT_ASSET.name,
                "hands": {
                    "left": SIDE_CONFIG["Left"]["asset"].name,
                    "right": SIDE_CONFIG["Right"]["asset"].name,
                },
                "authored_rifle_mounts_world": mounts,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )

    bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)


if __name__ == "__main__":
    main()
