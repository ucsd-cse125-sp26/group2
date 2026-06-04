from pathlib import Path

import bpy
from mathutils import Vector


ROOT = Path("/home/user/Documents/dev/group2")
IN_BLEND = ROOT / "assets/blender_sources/r301_wraith_arm_graft_user_stitched.blend"
OUT_BLEND = ROOT / "assets/blender_sources/r301_wraith_arm_graft_runtime_export.blend"
OUT_GLB = ROOT / "assets/animations/character_rigged_apex_hands.glb"

CURRENT_ARMATURE = "CURRENT_mixamo_reference_rig"
APEX_ARMATURE = "APEX_wraith_gameplay_arm_rig"
COMBINED_ARMATURE = "RUNTIME_mixamo_apex_combined_rig"
APEX_PARENT_BONE = "mixamorig:Spine2"
APEX_HELPER_BONES = {
    "ja_c_propGun",
    "weapon_bone",
    "muzzle_flash",
}


def require_object(name: str, expected_type: str):
    obj = bpy.data.objects.get(name)
    if obj is None:
        raise RuntimeError(f"Missing object {name}")
    if obj.type != expected_type:
        raise RuntimeError(f"{name} is {obj.type}, expected {expected_type}")
    return obj


def is_apex_group_name(name: str) -> bool:
    return (
        name.startswith("def_")
        or name.startswith("ja_")
        or name.startswith("jx_")
        or name.startswith("att_")
    )


def group_has_weight(mesh, group_name: str) -> bool:
    group = mesh.vertex_groups.get(group_name)
    if group is None:
        return False
    index = group.index
    for vertex in mesh.data.vertices:
        for assignment in vertex.groups:
            if assignment.group == index and assignment.weight > 1e-5:
                return True
    return False


def vertex_uses_apex_group(mesh, vertex) -> bool:
    group_names = {group.index: group.name for group in mesh.vertex_groups}
    return any(
        is_apex_group_name(group_names.get(assignment.group, "")) and assignment.weight > 1e-5
        for assignment in vertex.groups
    )


def weighted_apex_groups(meshes) -> set[str]:
    names: set[str] = set()
    for mesh in meshes:
        for group in mesh.vertex_groups:
            if is_apex_group_name(group.name) and group_has_weight(mesh, group.name):
                names.add(group.name)
    return names


def add_ancestors(armature, bone_names: set[str]) -> set[str]:
    out = set(bone_names)
    for name in list(bone_names):
        bone = armature.data.bones.get(name)
        while bone is not None:
            out.add(bone.name)
            bone = bone.parent
    return out


def duplicate_current_armature(current):
    existing = bpy.data.objects.get(COMBINED_ARMATURE)
    if existing is not None:
        bpy.data.objects.remove(existing, do_unlink=True)

    combined = current.copy()
    combined.data = current.data.copy()
    combined.name = COMBINED_ARMATURE
    combined.data.name = COMBINED_ARMATURE + "_data"
    combined.matrix_world = current.matrix_world.copy()
    bpy.context.collection.objects.link(combined)
    return combined


def copy_apex_bones_into_combined(apex, combined, bone_names: set[str]) -> int:
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = combined
    combined.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")

    edit_bones = combined.data.edit_bones
    combined_inv = combined.matrix_world.inverted()
    created = 0

    ordered = sorted(
        bone_names,
        key=lambda name: len(list(apex.data.bones[name].parent_recursive)) if name in apex.data.bones else 0,
    )
    for name in ordered:
        if name not in apex.data.bones:
            continue
        if name in edit_bones:
            continue

        source_bone = apex.data.bones[name]
        source_world = apex.matrix_world @ source_bone.matrix_local
        head = combined_inv @ (apex.matrix_world @ source_bone.head_local)
        tail = combined_inv @ (apex.matrix_world @ source_bone.tail_local)
        if (tail - head).length < 1e-5:
            tail = head + Vector((0.0, 0.01, 0.0))

        bone = edit_bones.new(name)
        bone.head = head
        bone.tail = tail
        bone.use_connect = False

        z_axis_world = (source_world.to_3x3() @ Vector((0.0, 0.0, 1.0))).normalized()
        z_axis_combined = (combined.matrix_world.inverted().to_3x3() @ z_axis_world).normalized()
        try:
            bone.align_roll(z_axis_combined)
        except Exception:
            pass

        if source_bone.parent and source_bone.parent.name in edit_bones:
            bone.parent = edit_bones[source_bone.parent.name]
        elif APEX_PARENT_BONE in edit_bones:
            bone.parent = edit_bones[APEX_PARENT_BONE]
        created += 1

    bpy.ops.object.mode_set(mode="OBJECT")
    return created


def sorted_bones_parent_first(armature, bone_names: set[str]) -> list[str]:
    return sorted(
        bone_names,
        key=lambda name: len(list(armature.data.bones[name].parent_recursive)) if name in armature.data.bones else 0,
    )


def pose_matrix_in_object_space(source, target, pose_bone_name: str):
    return target.matrix_world.inverted() @ source.matrix_world @ source.pose.bones[pose_bone_name].matrix


def pose_point_in_object_space(source, target, point):
    return target.matrix_world.inverted() @ (source.matrix_world @ point)


def rigid_matrix(matrix):
    rotation = matrix.to_3x3()
    rotation.normalize()
    out = rotation.to_4x4()
    out.translation = matrix.to_translation()
    return out


def bake_apex_frame_pose_into_rest(apex, current, combined, bone_names: set[str]) -> int:
    bpy.context.view_layer.update()
    source_pose = {
        name: rigid_matrix(pose_matrix_in_object_space(apex, combined, name))
        for name in bone_names
        if name in apex.pose.bones
    }
    source_heads = {
        name: pose_point_in_object_space(apex, combined, apex.pose.bones[name].head)
        for name in source_pose
    }
    source_tails = {
        name: pose_point_in_object_space(apex, combined, apex.pose.bones[name].tail)
        for name in source_pose
    }
    if APEX_PARENT_BONE not in current.pose.bones:
        raise RuntimeError(f"Missing Mixamo pose parent {APEX_PARENT_BONE}")
    parent_pose = {
        APEX_PARENT_BONE: rigid_matrix(pose_matrix_in_object_space(current, combined, APEX_PARENT_BONE)),
    }

    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = combined
    combined.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")

    edit_bones = combined.data.edit_bones
    rest_pose = {
        APEX_PARENT_BONE: rigid_matrix(edit_bones[APEX_PARENT_BONE].matrix.copy()),
    }
    baked = 0

    for name in sorted_bones_parent_first(apex, set(source_pose.keys())):
        if name not in edit_bones:
            continue

        source_bone = apex.data.bones[name]
        source_parent = source_bone.parent.name if source_bone.parent else ""
        if source_parent in source_pose and source_parent in edit_bones:
            parent_name = source_parent
        else:
            parent_name = APEX_PARENT_BONE

        parent_pose_matrix = parent_pose[parent_name] if parent_name == APEX_PARENT_BONE else source_pose[parent_name]
        parent_rest_matrix = rest_pose[parent_name]
        local_from_parent_pose = parent_pose_matrix.inverted() @ source_pose[name]
        rest_matrix = parent_rest_matrix @ local_from_parent_pose

        local_head = parent_pose_matrix.inverted() @ source_heads[name]
        local_tail = parent_pose_matrix.inverted() @ source_tails[name]
        head = parent_rest_matrix @ local_head
        tail = parent_rest_matrix @ local_tail
        if (tail - head).length < 1e-5:
            tail = rest_matrix @ Vector((0.0, 0.01, 0.0))

        bone = edit_bones[name]
        bone.head = head
        bone.tail = tail
        bone.use_connect = False

        z_axis = (rest_matrix.to_3x3() @ Vector((0.0, 0.0, 1.0))).normalized()
        try:
            bone.align_roll(z_axis)
        except Exception:
            pass

        rest_pose[name] = rigid_matrix(bone.matrix.copy())
        baked += 1

    bpy.ops.object.mode_set(mode="OBJECT")
    return baked


def bone_skin_matrix(armature, bone_name: str):
    rest = armature.data.bones[bone_name].matrix_local
    pose = armature.pose.bones[bone_name].matrix
    return pose @ rest.inverted()


def vertex_skin_matrix(mesh, vertex, skin_matrices):
    out = None
    group_names = {group.index: group.name for group in mesh.vertex_groups}
    for assignment in vertex.groups:
        if assignment.weight <= 1e-5:
            continue
        group_name = group_names.get(assignment.group, "")
        matrix = skin_matrices.get(group_name)
        if matrix is None:
            continue
        weighted = matrix * assignment.weight
        out = weighted if out is None else out + weighted
    return out


def bake_apex_weighted_vertices_to_source_pose(meshes, combined) -> int:
    bpy.context.view_layer.update()
    skin_matrices = {
        bone.name: bone_skin_matrix(combined, bone.name)
        for bone in combined.data.bones
        if bone.name in combined.pose.bones
    }
    depsgraph = bpy.context.evaluated_depsgraph_get()
    baked = 0
    for mesh in meshes:
        apex_indices = [vertex.index for vertex in mesh.data.vertices if vertex_uses_apex_group(mesh, vertex)]
        if not apex_indices:
            continue

        evaluated = mesh.evaluated_get(depsgraph)
        evaluated_mesh = evaluated.to_mesh()
        if len(evaluated_mesh.vertices) != len(mesh.data.vertices):
            evaluated.to_mesh_clear()
            raise RuntimeError(
                f"Cannot pose-bake {mesh.name}: evaluated vertex count changed "
                f"{len(mesh.data.vertices)} -> {len(evaluated_mesh.vertices)}"
            )

        evaluated_to_source_local = mesh.matrix_world.inverted() @ evaluated.matrix_world
        for index in apex_indices:
            skin_matrix = vertex_skin_matrix(mesh, mesh.data.vertices[index], skin_matrices)
            if skin_matrix is None:
                continue
            target = evaluated_to_source_local @ evaluated_mesh.vertices[index].co
            mesh.data.vertices[index].co = skin_matrix.inverted() @ target
        evaluated.to_mesh_clear()
        mesh.data.update()
        baked += len(apex_indices)
    return baked


def preserve_world_parent_to(mesh, armature):
    world = mesh.matrix_world.copy()
    mesh.parent = armature
    mesh.parent_type = "OBJECT"
    mesh.parent_bone = ""
    mesh.matrix_world = world


def retarget_meshes_to_combined(meshes, combined):
    for mesh in meshes:
        for modifier in list(mesh.modifiers):
            if modifier.type == "ARMATURE":
                mesh.modifiers.remove(modifier)
        modifier = mesh.modifiers.new("Armature", "ARMATURE")
        modifier.object = combined
        preserve_world_parent_to(mesh, combined)


def create_socket_keepalive_mesh(combined, bone_names: set[str]):
    keep_bones = [name for name in sorted(bone_names) if name in combined.data.bones]
    if not keep_bones:
        return None

    existing = bpy.data.objects.get("RUNTIME_socket_keepalive")
    if existing is not None:
        bpy.data.objects.remove(existing, do_unlink=True)

    mesh_data = bpy.data.meshes.new("RUNTIME_socket_keepalive_mesh")
    mesh_data.from_pydata([(0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)], [], [(0, 1, 2)])
    mesh_data.update()
    mesh = bpy.data.objects.new("RUNTIME_socket_keepalive", mesh_data)
    bpy.context.collection.objects.link(mesh)

    for name in keep_bones:
        group = mesh.vertex_groups.new(name=name)
        group.add([0, 1, 2], 1.0, "ADD")

    modifier = mesh.modifiers.new("Armature", "ARMATURE")
    modifier.object = combined
    preserve_world_parent_to(mesh, combined)
    return mesh


def export_runtime_glb(combined, meshes):
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="DESELECT")
    combined.select_set(True)
    for mesh in meshes:
        mesh.select_set(True)
    bpy.context.view_layer.objects.active = combined

    bpy.ops.export_scene.gltf(
        filepath=str(OUT_GLB),
        export_format="GLB",
        use_selection=True,
        export_skins=True,
        export_animations=False,
        export_yup=False,
        export_apply=False,
        export_rest_position_armature=True,
        export_def_bones=False,
        export_armature_object_remove=False,
        export_anim_single_armature=True,
    )


def main():
    bpy.ops.wm.open_mainfile(filepath=str(IN_BLEND))
    bpy.context.scene.frame_set(0)
    current = require_object(CURRENT_ARMATURE, "ARMATURE")
    apex = require_object(APEX_ARMATURE, "ARMATURE")
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH" and obj.visible_get()]
    if not meshes:
        raise RuntimeError("No visible meshes found for runtime export")

    apex_groups = weighted_apex_groups(meshes) | {name for name in APEX_HELPER_BONES if name in apex.data.bones}
    apex_groups = add_ancestors(apex, apex_groups)
    combined = duplicate_current_armature(current)
    created = copy_apex_bones_into_combined(apex, combined, apex_groups)
    baked = bake_apex_frame_pose_into_rest(apex, current, combined, apex_groups)
    baked_vertices = bake_apex_weighted_vertices_to_source_pose(meshes, combined)
    retarget_meshes_to_combined(meshes, combined)
    socket_keepalive = create_socket_keepalive_mesh(combined, APEX_HELPER_BONES)
    if socket_keepalive is not None:
        meshes.append(socket_keepalive)

    current.hide_viewport = True
    current.hide_render = True
    apex.hide_viewport = True
    apex.hide_render = True

    bpy.ops.wm.save_as_mainfile(filepath=str(OUT_BLEND))
    export_runtime_glb(combined, meshes)

    print(
        {
            "output_blend": str(OUT_BLEND),
            "output_glb": str(OUT_GLB),
            "meshes": [mesh.name for mesh in meshes],
            "apex_weighted_groups": len(apex_groups),
            "apex_bones_copied": created,
            "apex_bones_pose_baked": baked,
            "apex_vertices_pose_baked": baked_vertices,
        }
    )


main()
