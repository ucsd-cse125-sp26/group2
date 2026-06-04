import math
import os
from pathlib import Path

import bpy
from mathutils import Matrix, Vector


ROOT = Path("/home/user/Documents/dev/group2")
CURRENT_CHARACTER = ROOT / "assets/animations/character_rigged_new.glb"
WRAITH_CHARACTER = ROOT / "assets/wraith.glb"
R301_WEAPON = ROOT / "assets/apex_r301.glb"
RELOAD_ANIM = ROOT / "assets/animations/anims_apex/apex_reload_rifle.glb"
OUT_DIR = ROOT / "assets/blender_sources"
OUT_BLEND = OUT_DIR / "r301_wraith_arm_graft_reload_preview.blend"
IMPORT_R301_PROXY = False


CURRENT_REMOVE_ROOTS = ("mixamorig:LeftArm", "mixamorig:RightArm")
APEX_ARM_PREFIXES = (
    "def_l_clav",
    "def_l_shoulder",
    "def_l_elbow",
    "def_l_forearm",
    "def_l_wrist",
    "def_l_fin",
    "def_r_clav",
    "def_r_shoulder",
    "def_r_elbow",
    "def_r_forearm",
    "def_r_wrist",
    "def_r_fin",
)
APEX_ARM_EXACT = {"ja_l_propHand", "ja_r_propHand"}


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for collection in (
        bpy.data.meshes,
        bpy.data.armatures,
        bpy.data.actions,
        bpy.data.materials,
        bpy.data.objects,
        bpy.data.collections,
    ):
        for datablock in list(collection):
            try:
                collection.remove(datablock)
            except RuntimeError:
                pass


def make_material(name, color, roughness=0.65):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = color
        bsdf.inputs["Roughness"].default_value = roughness
    mat.diffuse_color = color
    return mat


def import_gltf(path):
    before = set(bpy.context.scene.objects)
    bpy.ops.import_scene.gltf(filepath=str(path))
    return [obj for obj in bpy.context.scene.objects if obj not in before]


def remove_helper_meshes(objects):
    remaining = []
    for obj in list(objects):
        obj_name = obj.name
        if obj_name.startswith("Icosphere") or obj_name.startswith("Sphere"):
            bpy.data.objects.remove(obj, do_unlink=True)
        else:
            remaining.append(obj)
    return remaining


def find_armature(objects, name_hint=None):
    armatures = [obj for obj in objects if obj.type == "ARMATURE"]
    if name_hint:
        for obj in armatures:
            if name_hint in obj.name:
                return obj
    if not armatures:
        raise RuntimeError(f"No armature found in imported objects: {[obj.name for obj in objects]}")
    return armatures[0]


def bone_descendants(armature, root_names):
    names = set()

    def visit(bone):
        names.add(bone.name)
        for child in bone.children:
            visit(child)

    for root_name in root_names:
        bone = armature.data.bones.get(root_name)
        if not bone:
            raise RuntimeError(f"Missing expected bone {root_name} on {armature.name}")
        visit(bone)
    return names


def vertex_weight_for_groups(obj, vertex, group_names):
    total = 0.0
    for assignment in vertex.groups:
        group = obj.vertex_groups[assignment.group]
        if group.name in group_names:
            total += assignment.weight
    return total


def delete_vertices(obj, should_delete):
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="DESELECT")
    bpy.ops.object.mode_set(mode="OBJECT")
    for vertex in obj.data.vertices:
        vertex.select = should_delete(vertex)
    obj.data.update()
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.delete(type="VERT")
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.data.update()


def assign_single_material(obj, material):
    obj.data.materials.clear()
    obj.data.materials.append(material)
    for poly in obj.data.polygons:
        poly.material_index = 0


def clip_current_arms(current_armature, current_meshes, material):
    remove_groups = bone_descendants(current_armature, CURRENT_REMOVE_ROOTS)
    stats = {}
    for mesh in current_meshes:
        before_vertices = len(mesh.data.vertices)
        before_faces = len(mesh.data.polygons)
        delete_vertices(
            mesh,
            lambda vertex, obj=mesh: vertex_weight_for_groups(obj, vertex, remove_groups) > 0.15,
        )
        assign_single_material(mesh, material)
        stats[mesh.name] = {
            "before_vertices": before_vertices,
            "after_vertices": len(mesh.data.vertices),
            "before_faces": before_faces,
            "after_faces": len(mesh.data.polygons),
        }
    return stats


def is_apex_arm_group(name):
    return name in APEX_ARM_EXACT or any(name.startswith(prefix) for prefix in APEX_ARM_PREFIXES)


def isolate_apex_arms(apex_meshes, material):
    stats = {}

    def keep_apex_arm_vertex(vertex):
        co = vertex.co
        broad_arm_band = 34.0 <= co.y <= 61.0 and abs(co.x) > 4.0
        clavicle_band = 49.0 <= co.y <= 60.5 and abs(co.x) > 1.35 and -7.0 <= co.z <= 8.0
        return broad_arm_band or clavicle_band

    for mesh in list(apex_meshes):
        mesh_name = mesh.name
        before_vertices = len(mesh.data.vertices)
        before_faces = len(mesh.data.polygons)
        if "wraith_lgnd_v24_hunter_body" not in mesh.name and "wraith_lgnd_v24_hunter_gear" not in mesh.name:
            bpy.data.objects.remove(mesh, do_unlink=True)
            stats[mesh_name] = {
                "before_vertices": before_vertices,
                "after_vertices": 0,
                "before_faces": before_faces,
                "after_faces": 0,
            }
            continue
        delete_vertices(mesh, lambda vertex: not keep_apex_arm_vertex(vertex))
        if len(mesh.data.polygons) == 0:
            bpy.data.objects.remove(mesh, do_unlink=True)
            stats[mesh_name] = {
                "before_vertices": before_vertices,
                "after_vertices": 0,
                "before_faces": before_faces,
                "after_faces": 0,
            }
            continue
        assign_single_material(mesh, material)
        mesh.name = "APEX_" + mesh.name
        stats[mesh.name] = {
            "before_vertices": before_vertices,
            "after_vertices": len(mesh.data.vertices),
            "before_faces": before_faces,
            "after_faces": len(mesh.data.polygons),
        }
    return stats


def aligned_apex_matrix(current_armature, apex_armature):
    # Pose-driven mapping:
    # Apex local X = side, Apex local Y = up, Apex local Z = forward.
    # The current character's imported action changes its visible side axis, so
    # derive side from current pose shoulders instead of assuming fixed axes.
    bpy.context.view_layer.update()
    left_current = (current_armature.matrix_world @ current_armature.pose.bones["mixamorig:LeftArm"].matrix).translation
    right_current = (current_armature.matrix_world @ current_armature.pose.bones["mixamorig:RightArm"].matrix).translation
    left_apex_rest = apex_armature.data.bones["def_l_shoulder"].head_local
    right_apex_rest = apex_armature.data.bones["def_r_shoulder"].head_local
    current_side_span = (left_current - right_current).length
    apex_side_span = abs(left_apex_rest.x - right_apex_rest.x)
    scale = current_side_span / apex_side_span
    current_mid = (left_current + right_current) * 0.5
    apex_mid = (left_apex_rest + right_apex_rest) * 0.5
    side_axis = (left_current - right_current).normalized()
    up_axis = Vector((0.0, 0.0, 1.0))
    forward_axis = side_axis.cross(up_axis).normalized()
    mapped_mid = side_axis * (scale * apex_mid.x) + up_axis * (scale * apex_mid.y) + forward_axis * (scale * apex_mid.z)
    translation = current_mid - mapped_mid
    return Matrix(
        (
            (side_axis.x * scale, up_axis.x * scale, forward_axis.x * scale, translation.x),
            (side_axis.y * scale, up_axis.y * scale, forward_axis.y * scale, translation.y),
            (side_axis.z * scale, up_axis.z * scale, forward_axis.z * scale, translation.z),
            (0.0, 0.0, 0.0, 1.0),
        )
    )


def world_bone_matrix(armature, bone_name):
    bpy.context.view_layer.update()
    return armature.matrix_world @ armature.pose.bones[bone_name].matrix


def import_reload_armature():
    imported = import_gltf(RELOAD_ANIM)
    reload_armature = find_armature(imported, "wraith_lgnd")
    reload_armature.name = "APEX_wraith_reload_driver_rig"
    reload_armature.hide_render = True
    reload_armature.hide_viewport = True
    reload_armature.show_in_front = False
    try:
        reload_armature.data.display_type = "STICK"
    except Exception:
        pass
    action = reload_armature.animation_data.action if reload_armature.animation_data else None
    if not action:
        raise RuntimeError("Imported reload armature did not have an assigned action")
    for obj in list(imported):
        if obj != reload_armature and obj.type == "MESH":
            bpy.data.objects.remove(obj, do_unlink=True)
    return reload_armature, action


def constrain_armature_to_driver(source_armature, driver_armature):
    constrained = []
    for pose_bone in source_armature.pose.bones:
        if not is_apex_arm_group(pose_bone.name):
            continue
        if pose_bone.name not in driver_armature.pose.bones:
            continue
        for constraint in list(pose_bone.constraints):
            pose_bone.constraints.remove(constraint)
        constraint = pose_bone.constraints.new(type="COPY_ROTATION")
        constraint.name = "copy_reload_driver_rotation"
        constraint.target = driver_armature
        constraint.subtarget = pose_bone.name
        constraint.target_space = "LOCAL"
        constraint.owner_space = "LOCAL"
        constrained.append(pose_bone.name)
    return constrained


def graft_meshes_to_armature(meshes, source_armature, target_armature):
    grafted = []
    for mesh in meshes:
        if mesh.name not in bpy.data.objects:
            continue
        if mesh.type != "MESH" or len(mesh.data.polygons) == 0:
            continue
        mesh.parent = target_armature
        mesh.matrix_parent_inverse.identity()
        mesh.location = (0.0, 0.0, 0.0)
        mesh.rotation_euler = (0.0, 0.0, 0.0)
        mesh.scale = (1.0, 1.0, 1.0)
        armature_mod = None
        for modifier in mesh.modifiers:
            if modifier.type == "ARMATURE":
                armature_mod = modifier
                break
        if armature_mod is None:
            armature_mod = mesh.modifiers.new("Armature", "ARMATURE")
        armature_mod.object = target_armature
        grafted.append(mesh.name)
    bpy.data.objects.remove(source_armature, do_unlink=True)
    return grafted


def parent_object_to_bone_preserve_world(obj, parent_armature, parent_bone_name):
    if parent_bone_name not in parent_armature.pose.bones:
        raise RuntimeError(f"Missing parent bone {parent_bone_name} on {parent_armature.name}")
    bpy.context.view_layer.update()
    world_before = obj.matrix_world.copy()
    obj.parent = parent_armature
    obj.parent_type = "BONE"
    obj.parent_bone = parent_bone_name
    obj.matrix_world = world_before
    bpy.context.view_layer.update()


def world_pose_bone_head(armature, bone_name):
    if bone_name not in armature.pose.bones:
        raise RuntimeError(f"Missing pose bone {bone_name} on {armature.name}")
    return armature.matrix_world @ armature.pose.bones[bone_name].head


def make_anchor(name, location, parent_armature, parent_bone_name, size=0.075):
    empty = bpy.data.objects.new(name, None)
    empty.empty_display_type = "SPHERE"
    empty.empty_display_size = size
    bpy.context.collection.objects.link(empty)
    empty.location = location
    parent_object_to_bone_preserve_world(empty, parent_armature, parent_bone_name)
    return empty


def add_copy_location_constraint(pose_bone, target, name, influence=1.0):
    constraint = pose_bone.constraints.new(type="COPY_LOCATION")
    constraint.name = name
    constraint.target = target
    constraint.target_space = "WORLD"
    constraint.owner_space = "WORLD"
    constraint.influence = influence
    return constraint


def create_shoulder_anchor_controls(current_armature, apex_armature):
    anchors = {
        "left_clav": make_anchor(
            "APEX_SOCKET_L_CLAVICLE",
            world_pose_bone_head(current_armature, "mixamorig:LeftShoulder"),
            current_armature,
            "mixamorig:Spine2",
        ),
        "right_clav": make_anchor(
            "APEX_SOCKET_R_CLAVICLE",
            world_pose_bone_head(current_armature, "mixamorig:RightShoulder"),
            current_armature,
            "mixamorig:Spine2",
        ),
        "left_arm": make_anchor(
            "APEX_SOCKET_L_UPPERARM_OPTIONAL",
            world_pose_bone_head(current_armature, "mixamorig:LeftArm"),
            current_armature,
            "mixamorig:Spine2",
            size=0.055,
        ),
        "right_arm": make_anchor(
            "APEX_SOCKET_R_UPPERARM_OPTIONAL",
            world_pose_bone_head(current_armature, "mixamorig:RightArm"),
            current_armature,
            "mixamorig:Spine2",
            size=0.055,
        ),
    }

    add_copy_location_constraint(
        apex_armature.pose.bones["def_l_clav"],
        anchors["left_clav"],
        "socket_attach_left_clavicle",
        influence=1.0,
    )
    add_copy_location_constraint(
        apex_armature.pose.bones["def_r_clav"],
        anchors["right_clav"],
        "socket_attach_right_clavicle",
        influence=1.0,
    )
    add_copy_location_constraint(
        apex_armature.pose.bones["def_l_shoulder"],
        anchors["left_arm"],
        "socket_attach_left_upperarm_optional",
        influence=0.0,
    )
    add_copy_location_constraint(
        apex_armature.pose.bones["def_r_shoulder"],
        anchors["right_arm"],
        "socket_attach_right_upperarm_optional",
        influence=0.0,
    )
    return {key: value.name for key, value in anchors.items()}


def import_r301_proxy(apex_armature, apex_matrix, material):
    if not IMPORT_R301_PROXY:
        return {"enabled": False, "reason": "disabled for arm animation preview"}
    if not R301_WEAPON.exists():
        return {"enabled": False, "reason": "missing R301 asset"}
    imported = import_gltf(R301_WEAPON)
    proxy_armature = find_armature(imported)
    proxy_armature.name = "R301_proxy_rig"
    proxy_armature.animation_data_clear()
    proxy_armature.matrix_world = apex_matrix.copy()
    proxy_mesh_names = []
    for obj in imported:
        if obj.type == "MESH" and obj.name.startswith("Icosphere"):
            bpy.data.objects.remove(obj, do_unlink=True)
            continue
        if obj.type == "MESH":
            assign_single_material(obj, material)
            obj.name = "R301_" + obj.name
            proxy_mesh_names.append(obj.name)
        elif obj.type == "ARMATURE":
            obj.hide_render = True
            obj.show_in_front = False
    bpy.context.view_layer.update()

    if "ja_c_propGun" in proxy_armature.data.bones and "ja_c_propGun" in apex_armature.pose.bones:
        target_head = world_bone_matrix(apex_armature, "ja_c_propGun").translation
        proxy_head = world_bone_matrix(proxy_armature, "ja_c_propGun").translation
        proxy_armature.matrix_world.translation += target_head - proxy_head
        bpy.context.view_layer.update()
        world_before = proxy_armature.matrix_world.copy()
        proxy_armature.parent = apex_armature
        proxy_armature.parent_type = "BONE"
        proxy_armature.parent_bone = "ja_c_propGun"
        proxy_armature.matrix_world = world_before
        bpy.context.view_layer.update()

    return {
        "enabled": True,
        "armature": proxy_armature.name,
        "meshes": proxy_mesh_names,
    }


def look_at(obj, target):
    direction = Vector(target) - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def add_camera_and_lights():
    light_data = bpy.data.lights.new("Key_Area", type="AREA")
    light_data.energy = 600
    light_data.size = 4
    light = bpy.data.objects.new("Key_Area", light_data)
    bpy.context.collection.objects.link(light)
    light.location = (2.5, -4.0, 4.0)

    camera_data = bpy.data.cameras.new("Preview_Camera")
    camera = bpy.data.objects.new("Preview_Camera", camera_data)
    bpy.context.collection.objects.link(camera)
    camera.location = (3.7, -6.2, 2.1)
    look_at(camera, (0.0, 0.0, 1.05))
    camera.data.lens = 42
    bpy.context.scene.camera = camera
    return camera


def render_frames():
    outputs = []
    bpy.context.scene.render.resolution_x = 1400
    bpy.context.scene.render.resolution_y = 1000
    try:
        bpy.context.scene.render.engine = "BLENDER_WORKBENCH"
        bpy.context.scene.display.shading.light = "STUDIO"
        bpy.context.scene.display.shading.color_type = "MATERIAL"
    except Exception:
        pass
    for frame in (0, 43, 86):
        bpy.context.scene.frame_set(frame)
        bpy.context.view_layer.update()
        out_path = OUT_DIR / f"r301_wraith_arm_graft_reload_frame_{frame:03d}.png"
        bpy.context.scene.render.filepath = str(out_path)
        bpy.ops.render.render(write_still=True)
        outputs.append(str(out_path))
    return outputs


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    clear_scene()

    body_mat = make_material("prototype_current_body_matte", (0.65, 0.67, 0.68, 1.0))
    apex_arm_mat = make_material("prototype_apex_arms_bluegray", (0.42, 0.50, 0.56, 1.0))
    weapon_mat = make_material("prototype_r301_dark", (0.08, 0.09, 0.10, 1.0))

    current_objects = import_gltf(CURRENT_CHARACTER)
    current_objects = remove_helper_meshes(current_objects)
    current_armature = find_armature(current_objects, "Armature")
    current_armature.name = "CURRENT_mixamo_reference_rig"
    current_armature.hide_render = True
    bpy.context.scene.frame_set(0)
    bpy.context.view_layer.update()
    current_meshes = [obj for obj in current_objects if obj.type == "MESH" and obj.parent == current_armature]
    current_stats = clip_current_arms(current_armature, current_meshes, body_mat)

    wraith_objects = import_gltf(WRAITH_CHARACTER)
    wraith_objects = remove_helper_meshes(wraith_objects)
    source_apex_armature = find_armature(wraith_objects, "wraith_lgnd")
    source_apex_armature.name = "APEX_wraith_gameplay_arm_rig"
    source_apex_armature.hide_render = True
    source_apex_armature.show_in_front = False
    try:
        source_apex_armature.data.display_type = "STICK"
    except Exception:
        pass
    apex_meshes = [obj for obj in wraith_objects if obj.type == "MESH" and obj.parent == source_apex_armature]
    apex_stats = isolate_apex_arms(apex_meshes, apex_arm_mat)

    driver_armature, reload_action = import_reload_armature()
    bpy.context.scene.frame_start = 0
    bpy.context.scene.frame_end = 86
    bpy.context.scene.frame_set(0)
    bpy.context.view_layer.update()
    apex_matrix = aligned_apex_matrix(current_armature, driver_armature)
    driver_armature.matrix_world = apex_matrix
    source_apex_armature.matrix_world = apex_matrix
    constrained_bones = constrain_armature_to_driver(source_apex_armature, driver_armature)
    bpy.context.view_layer.update()
    parent_object_to_bone_preserve_world(source_apex_armature, current_armature, "mixamorig:Spine2")
    parent_object_to_bone_preserve_world(driver_armature, current_armature, "mixamorig:Spine2")
    anchor_names = create_shoulder_anchor_controls(current_armature, source_apex_armature)

    weapon_stats = import_r301_proxy(source_apex_armature, apex_matrix, weapon_mat)
    bpy.context.scene.frame_set(43)

    add_camera_and_lights()
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT_BLEND))
    rendered = render_frames()

    print(
        {
            "output_blend": str(OUT_BLEND),
            "rendered": rendered,
            "reload_action": reload_action.name,
            "frame_range": tuple(float(v) for v in reload_action.frame_range),
            "current_clip": current_stats,
            "apex_arm_clip": apex_stats,
            "constrained_bones": len(constrained_bones),
            "anchors": anchor_names,
            "weapon": weapon_stats,
        }
    )


main()
