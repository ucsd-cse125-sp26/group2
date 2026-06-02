"""Convert an Apex weapon into the engine's unified animated-viewmodel assets.

Produces, from a weapon's first-person ("_v") cast + textures + first-person
clips, the two GLBs the per-weapon viewmodel system consumes:

    assets/apex_<weapon>.glb       gun: textured, carries `ja_c_propGun`, baked 1P clips
    assets/apex_<weapon>_arms.glb  arms: shared pilot arms + the SAME baked 1P clips

The same `apex_<weapon>.glb` is used for first-person AND third-person (its
`ja_c_propGun` bone mounts it on the character). After running this, add a row to
`kWeaponViewmodelAssets` in src/ecs/AssetCatalog.hpp:
    {.viewmodelGlb = "apex_<weapon>.glb", .armsGlb = "apex_<weapon>_arms.glb", .flipUVs = <bool>}
— no other code changes, no per-weapon offset tuning.

HOW TO RUN
    Open Blender (5.x) with the io_scene_cast add-on enabled and a 3D viewport,
    then run this file from the Scripting tab, OR paste its body through the
    Blender MCP `execute_blender_code` tool. (The cast importer needs a VIEW_3D
    context, so headless `--background` will not work for the import step.)

RSX EXTRACTION CHECKLIST (do this in the RSX tool first — these assets are NOT
in a default dump):
    1. The weapon's FIRST-PERSON model: `<weapon>_lgnd_*_v` (the `_v` = viewmodel
       variant), LOD0 — export the .cast AND its material textures
       (`<material>_col.png`, `_nml.png`, optional `_ilm.png`).
    2. The weapon's FIRST-PERSON clips: `bulk_anims/animseq/weapons/<weapon-script-
       name>/ptpov_<weapon>/` — at least `idle`, `draw`, `reload` (+ `fire` if you
       want a chamber kick). Find the weapon's script-name folder in RSX (snipers
       live under names like `defender`/`sentinel`; the Kraber's wasn't in the
       sample dump and must be exported).
    3. The shared pilot ARMS: `pov_pilot_light_wraith_LOD0.cast` (+ its textures) —
       reused for every light-legend weapon's hands.

Then fill in CONFIG below and run.
"""

import bpy
import os

# ----------------------------------------------------------------------------
# CONFIG — edit per weapon, then run. Example values are for the Kraber.
# ----------------------------------------------------------------------------
RSX = r"C:\Users\user99\Downloads\rsx_2.1.0"
OUT_DIR = r"C:\Users\user99\CLionProjects\group2\assets"
BLEND_DIR = r"C:\Users\user99\CLionProjects\group2\assets\blender_sources"

CONFIG = {
    "weapon_name": "chargerifle",
    # First-person model = the Charge Rifle's ptpov ("defender" is Apex's codename for
    # the Charge Rifle). Exported via the RSX GUI to defender_gui_export/. Rig: 139
    # bones incl. ja_c_propGun + muzzle_flash + character spine — a real viewmodel rig.
    "gun_cast":   RSX + r"\defender_gui_export\mdl\ptpov_defender\ptpov_defender_LOD0.cast",
    # Body textures (chargerifle_base_*) were NOT in the ptpov_defender export — drop
    # them here named `chargerifle_base_main_col.png` etc. when the full pak dump lands.
    "gun_texdir": RSX + r"\defender_gui_export\mdl\ptpov_defender",
    # Shared pilot arms + their textures (same for every light-legend weapon).
    "arms_cast":   RSX + r"\extract_test\mdl\pov_pilot_light_wraith\pov_pilot_light_wraith_LOD0.cast",
    "arms_texdir": RSX + r"\extract_test\mdl\pov_pilot_light_wraith\pov_pilot_light_wraith",
    # First-person clip dir + {cast-basename (no .cast, may include subdir): action}.
    # These are the Charge Rifle's REAL camera-space viewmodel clips (ptpov_defender),
    # so the gun + pilot arms animate correctly in first-person (unlike the Kraber,
    # which had only a 3rd-person pilot reload). The engine plays "draw" on equip and
    # "reload" on reload; idle/fire are kept for completeness.
    "clip_dir": RSX + r"\defender_gui_export\animseq",
    "clips": {
        "draw/draw_0": "draw",
        "reload/reload_0": "reload",
        "idle/idle_0": "idle",
        "fire/fire_0": "fire",
        # Crouch lower/raise transitions (Apex "gun feel"): each ends at the crouch
        # / standing idle pose; the engine plays one per crouch toggle and holds it.
        "idle_to_crouch/idle_to_crouch_0": "idle_to_crouch",
        "crouch_to_idle/crouch_to_idle_0": "crouch_to_idle",
    },
    # Keep only the gun body (main + charge coils + reloader + front sight); drop every
    # scope/sight/suppressor/laser attachment mesh.
    "keep_mesh_substr": ["chargerifle_base"],
}


# ----------------------------------------------------------------------------
# helpers
# ----------------------------------------------------------------------------
def _fv3d():
    for w in bpy.context.window_manager.windows:
        for a in w.screen.areas:
            if a.type == "VIEW_3D":
                return w, w.screen, a, next((r for r in a.regions if r.type == "WINDOW"), None)
    return (None,) * 4


def _import_cast(path, onto_armature=None):
    """Import a .cast. If onto_armature is given, the clip's action lands on it."""
    w, s, a, r = _fv3d()
    sel = {}
    if onto_armature is not None:
        for o in bpy.data.objects:
            o.select_set(False)
        onto_armature.select_set(True)
        bpy.context.view_layer.objects.active = onto_armature
        sel = dict(active_object=onto_armature, selected_objects=[onto_armature])
    with bpy.context.temp_override(window=w, screen=s, area=a, region=r, **sel):
        bpy.ops.import_scene.cast(filepath=path)


def _force_clear_actions():
    # Blender 5.x layered-action gotcha: remove EVERY action regardless of users,
    # else suffixed (.001/.003) leftovers get exported instead of the intended clip.
    for o in bpy.data.objects:
        if o.animation_data:
            o.animation_data.action = None
    for act in list(bpy.data.actions):
        try:
            bpy.data.actions.remove(act)
        except Exception:
            pass


def _wire_textures(texdir):
    """For every material, wire <material>_col -> Base Color, _nml -> Normal,
    _ilm -> Emission (sRGB for col/ilm, Non-Color for nml)."""
    for mat in list(bpy.data.materials):
        if not mat.use_nodes:
            mat.use_nodes = True
        nt = mat.node_tree
        bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if not bsdf:
            continue

        def tex(suffix, non_color, y):
            p = os.path.join(texdir, "%s_%s.png" % (mat.name, suffix))
            if not os.path.exists(p):
                return None
            img = bpy.data.images.load(p, check_existing=True)
            img.colorspace_settings.name = "Non-Color" if non_color else "sRGB"
            n = nt.nodes.new("ShaderNodeTexImage")
            n.image = img
            n.location = (-800, y)
            return n

        col = tex("col", False, 300)
        if col:
            nt.links.new(col.outputs["Color"], bsdf.inputs["Base Color"])
        nml = tex("nml", True, 0)
        if nml:
            nm = nt.nodes.new("ShaderNodeNormalMap")
            nm.location = (-450, 0)
            nt.links.new(nml.outputs["Color"], nm.inputs["Color"])
            nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
        ilm = tex("ilm", False, -300)
        if ilm and "Emission Color" in bsdf.inputs:
            nt.links.new(ilm.outputs["Color"], bsdf.inputs["Emission Color"])
            bsdf.inputs["Emission Strength"].default_value = 1.0


def _bake_clips(rig, clip_dir, clips):
    """Import each first-person clip onto `rig` as a named, fake-user action."""
    loaded = []
    for basename, action_name in clips.items():
        p = os.path.join(clip_dir, basename + ".cast")
        if not os.path.exists(p):
            continue
        _import_cast(p, onto_armature=rig)
        act = rig.animation_data.action if rig.animation_data else None
        if act:
            act.name = action_name
            act.use_fake_user = True  # survive the export (otherwise zero-user clips drop)
            loaded.append(action_name)
    return loaded


def _export_glb(out_path, active):
    # +Y up, all ACTIONS, skins, NO transform bake (export_apply=False) — the bake
    # was the bug in the old kraber export that flattened the rig and dropped textures.
    # Wrap in a VIEW_3D + active/selected override: the glTF exporter reads
    # bpy.context.active_object, which is absent in a headless/MCP exec context.
    w, s, a, r = _fv3d()
    sel = [o for o in bpy.data.objects if o.type in ("ARMATURE", "MESH")]
    with bpy.context.temp_override(window=w, screen=s, area=a, region=r,
                                   active_object=active, selected_objects=sel):
        bpy.ops.export_scene.gltf(
            filepath=out_path, export_format="GLB",
            export_yup=True, export_apply=False,
            export_animation_mode="ACTIONS", export_anim_single_armature=True,
            export_skins=True, export_normals=True, export_tangents=True,
            export_texcoords=True, export_materials="EXPORT",
        )


def _build(cast, texdir, clip_dir, clips, out_glb, blend_out, keep_substr):
    bpy.ops.wm.read_homefile(use_empty=True)
    _import_cast(cast)
    rig = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    # Drop unwanted attachment meshes (optics/suppressors/etc.).
    if keep_substr:
        for o in [o for o in bpy.data.objects if o.type == "MESH"]:
            if not any(k in o.name for k in keep_substr):
                bpy.data.objects.remove(o, do_unlink=True)
    _wire_textures(texdir)
    _force_clear_actions()
    actions = _bake_clips(rig, clip_dir, clips)
    # Export the whole scene (armature + skinned meshes + actions).
    for o in bpy.data.objects:
        o.select_set(o.type in ("ARMATURE", "MESH"))
    bpy.context.view_layer.objects.active = rig
    _export_glb(out_glb, rig)
    try:
        bpy.ops.wm.save_as_mainfile(filepath=blend_out, copy=True)
    except Exception:
        pass
    return {"out": out_glb, "exists": os.path.exists(out_glb),
            "size_kb": (os.path.getsize(out_glb) // 1024) if os.path.exists(out_glb) else 0,
            "meshes": [o.name for o in bpy.data.objects if o.type == "MESH"],
            "has_propGun": any(b.name == "ja_c_propGun" for b in rig.data.bones),
            "actions": actions}


def run(cfg=CONFIG):
    os.makedirs(OUT_DIR, exist_ok=True)
    os.makedirs(BLEND_DIR, exist_ok=True)
    name = cfg["weapon_name"]
    gun = _build(cfg["gun_cast"], cfg["gun_texdir"], cfg["clip_dir"], cfg["clips"],
                 os.path.join(OUT_DIR, "apex_%s.glb" % name),
                 os.path.join(BLEND_DIR, "%s_viewmodel.blend" % name),
                 cfg["keep_mesh_substr"])
    arms = _build(cfg["arms_cast"], cfg["arms_texdir"], cfg["clip_dir"], cfg["clips"],
                  os.path.join(OUT_DIR, "apex_%s_arms.glb" % name),
                  os.path.join(BLEND_DIR, "%s_arms.blend" % name),
                  [])  # keep all arm meshes
    print("=== gun  ===", gun)
    print("=== arms ===", arms)
    if not gun["has_propGun"]:
        print("WARNING: gun rig has no ja_c_propGun bone — third-person mount will "
              "fall back to the wrist+palm path. Use a viewmodel ('_v') cast.")
    return {"gun": gun, "arms": arms}


# `result` is what the Blender MCP returns; `run()` also works from the Scripting tab.
result = run()
