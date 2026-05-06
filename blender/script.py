import bpy

# ============================================================
# Collision-Mesh Generator (PR-31: type-prefixed output)
# ============================================================
#
# Walks the scene, duplicates eligible source meshes into a `Collision`
# collection, and emits each one with a type-prefixed name that the
# C++ map loader (`src/ecs/physics/MapLoader.cpp`) dispatches on:
#
#     COL_BOX_<source>       → axis-aligned box
#     COL_RAMP_<source>      → 5-plane wedge brush
#     COL_BRUSH_<source>     → generic convex brush
#     COL_CYL_<source>       → vertical cylinder
#     COL_SPHERE_<source>    → sphere
#     COL_PLATFORM_<source>  → thin axis-aligned platform (auto-extruded)
#     COL_MESH_<source>      → triangle mesh (verbatim, slowest collision)
#     COL_<source>           → no type prefix → C++ auto-detects
#
# Authoring path
# --------------
# Per source object:
#
#   1. Add a custom property `collision_type` to override the default.
#      Recognised values (case-insensitive):
#          AUTO (default) — let the script + C++ figure it out
#          BOX, AABB
#          RAMP, WEDGE
#          BRUSH, CONVEX
#          CYL, CYLINDER
#          SPHERE
#          PLATFORM, PLANE
#          MESH, TRIMESH
#          NONE — skip collision entirely for this object
#   2. If the property is missing or AUTO, the script tries to detect
#      the shape from face count + topology (see `auto_detect_shape`).
#      Anything it cannot identify is emitted with no type prefix
#      so the C++ loader's geometric heuristics get a chance.
#
# The pre-PR-31 behaviour (decimate complex meshes, leave simple ones
# alone, name everything `COL_<source>`) is preserved when every
# object's `collision_type` is AUTO and detection fails.

# -----------------------------
# Settings
# -----------------------------

COLLECTION_NAME = "Collision"
COLLISION_PREFIX = "COL_"

# Lower = more simplified for complex meshes.
DECIMATE_RATIO = 0.35

# Objects with this many faces or fewer will NOT be decimated.
DO_NOT_DECIMATE_FACE_LIMIT = 12

# Do not allow decimated meshes to end up below this many faces.
MIN_COLLISION_FACES = 8

ONLY_SELECTED = False

HIDE_COLLISION_RENDER = True
HIDE_COLLISION_VIEWPORT = False
DISPLAY_AS_WIRE = True

CLEAR_OLD_GENERATED_COLLISION = True

# PR-31: shape-detection thresholds.  All in *world* units (after
# applying object scale) so dimensions match what the C++ loader sees.

# How close a face normal must be to a world axis to count as "axis
# aligned" for shape classification.  Matches the C++ loader's
# `k_normalAxisTolerance = 1e-3` (0.06°).
AXIS_NORMAL_TOLERANCE = 1e-3

# A "thin platform" is any mesh whose bounding-box extent on at least
# one world axis is below this fraction of the largest extent.
# 0.05 catches a 1-unit-tall floor inside a 20-unit-wide room.
PLATFORM_THIN_AXIS_RATIO = 0.05

# Recognised values for the `collision_type` custom property.
# Anything not in this set falls back to AUTO.  Case-insensitive.
EXPLICIT_TYPES = {
    "BOX": "BOX",
    "AABB": "BOX",
    "RAMP": "RAMP",
    "WEDGE": "RAMP",
    "BRUSH": "BRUSH",
    "CONVEX": "BRUSH",
    "CYL": "CYL",
    "CYLINDER": "CYL",
    "SPHERE": "SPHERE",
    "PLATFORM": "PLATFORM",
    "PLANE": "PLATFORM",
    "MESH": "MESH",
    "TRIMESH": "MESH",
}


# -----------------------------
# Helper functions
# -----------------------------

def ensure_object_mode():
    active = bpy.context.view_layer.objects.active

    if active is not None and active.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")


def get_or_create_collection(name):
    collection = bpy.data.collections.get(name)

    if collection is None:
        collection = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(collection)

    return collection


def link_to_collection(obj, collection):
    if obj.name not in collection.objects.keys():
        collection.objects.link(obj)


def move_to_collection(obj, collection):
    link_to_collection(obj, collection)

    for old_collection in list(obj.users_collection):
        if old_collection != collection:
            old_collection.objects.unlink(obj)


def make_collision_name(source_name, type_word):
    """
    Build the COL_<TYPE>_<source> name (or COL_<source> when type is AUTO).
    Strips redundant prefixes (`MESH_`, `RENDER_`, `VIS_`, `OBJ_`) from the
    source name first so the resulting name doesn't carry historical noise.
    """
    clean_name = source_name

    for prefix in ["MESH_", "RENDER_", "VIS_", "OBJ_"]:
        if clean_name.startswith(prefix):
            clean_name = clean_name[len(prefix):]
            break

    if type_word and type_word != "AUTO":
        base_name = f"{COLLISION_PREFIX}{type_word}_{clean_name}"
    else:
        base_name = COLLISION_PREFIX + clean_name

    if base_name not in bpy.data.objects:
        return base_name

    i = 1
    while f"{base_name}_{i:03d}" in bpy.data.objects:
        i += 1

    return f"{base_name}_{i:03d}"


def should_generate_collision(obj):
    if obj.type != "MESH":
        return False

    if obj.name.startswith(COLLISION_PREFIX):
        return False

    collision_type = str(obj.get("collision_type", "AUTO")).upper()

    if collision_type == "NONE":
        return False

    return True


def get_explicit_type(obj):
    """
    Return the canonical type word from the source object's `collision_type`
    custom property, or "AUTO" if it's missing / unrecognised.
    """
    raw = str(obj.get("collision_type", "AUTO")).upper()
    return EXPLICIT_TYPES.get(raw, "AUTO")


def auto_detect_shape(obj):
    """
    Best-effort geometric classification when the user hasn't set
    `collision_type`.  Returns one of the canonical type words above,
    or "AUTO" if nothing matches confidently — the C++ loader's
    heuristics get the final say in that case.

    Detection rules (all on the source object's mesh, BEFORE decimation):

        BOX       — exactly 6 quads / 12 tris with all face normals
                    aligned to world axes (a Blender default cube).
        PLATFORM  — flat / near-flat: at least one bounding-box axis
                    has < PLATFORM_THIN_AXIS_RATIO of the largest extent.
                    Common pattern for floors/decks built as planes.
        RAMP      — exactly 5 unique face normals, one of which is
                    NOT axis-aligned (the slope), the rest are.
                    Triangle count irrelevant (Blender's wedge has 8
                    tris = 5 faces; 2 of those are the triangular
                    side caps, 3 are quad-pairs).
        Otherwise — AUTO (let C++ decide).
    """
    mesh = obj.data
    if mesh is None or len(mesh.vertices) == 0:
        return "AUTO"

    # World-space normals + bounding box.  We bake the object's scale
    # into the box so PLATFORM_THIN_AXIS_RATIO matches what the C++
    # loader will compute after `MapLoadOptions.scale`.
    world_mat = obj.matrix_world

    bmin = [float("inf")] * 3
    bmax = [float("-inf")] * 3
    for v in mesh.vertices:
        wp = world_mat @ v.co
        for i in range(3):
            if wp[i] < bmin[i]:
                bmin[i] = wp[i]
            if wp[i] > bmax[i]:
                bmax[i] = wp[i]
    extents = [bmax[i] - bmin[i] for i in range(3)]
    max_ext = max(extents) if extents else 0.0
    if max_ext <= 0.0:
        return "AUTO"

    # PLATFORM: any axis below the thin threshold → flat-ish shape.
    if any(e < max_ext * PLATFORM_THIN_AXIS_RATIO for e in extents):
        return "PLATFORM"

    # Collect unique face normals (deduplicated by direction within
    # AXIS_NORMAL_TOLERANCE).  Each polygon's `.normal` is in object
    # space; rotate to world space.
    unique_normals = []

    def normal_already_present(n):
        for existing in unique_normals:
            if abs(existing.dot(n) - 1.0) < AXIS_NORMAL_TOLERANCE:
                return True
        return False

    rot_only = world_mat.to_3x3()
    for poly in mesh.polygons:
        wn = (rot_only @ poly.normal).normalized()
        if not normal_already_present(wn):
            unique_normals.append(wn)
            if len(unique_normals) > 8:  # bigger than any primitive we care about
                break

    if len(unique_normals) == 6 and all_axis_aligned(unique_normals):
        return "BOX"

    if len(unique_normals) == 5:
        axis_count = sum(1 for n in unique_normals if is_axis_aligned(n))
        if axis_count == 4:
            return "RAMP"

    return "AUTO"


def is_axis_aligned(n):
    """True iff the normal is within AXIS_NORMAL_TOLERANCE of a world axis."""
    for axis in range(3):
        if abs(abs(n[axis]) - 1.0) < AXIS_NORMAL_TOLERANCE:
            return True
    return False


def all_axis_aligned(normals):
    return all(is_axis_aligned(n) for n in normals)


def clear_old_generated_collision():
    old_objects = [
        obj for obj in bpy.data.objects
        if obj.get("generated_collision", False) is True
    ]

    for obj in old_objects:
        bpy.data.objects.remove(obj, do_unlink=True)

    print(f"Removed {len(old_objects)} old generated collision objects.")


def duplicate_as_collision_object(source_obj, collision_collection, type_word):
    collision_obj = source_obj.copy()
    collision_obj.data = source_obj.data.copy()

    collision_obj.name = make_collision_name(source_obj.name, type_word)
    collision_obj.data.name = collision_obj.name + "_Mesh"

    link_to_collection(collision_obj, collision_collection)
    move_to_collection(collision_obj, collision_collection)

    collision_obj["is_collision"] = True
    collision_obj["generated_collision"] = True
    collision_obj["source_object"] = source_obj.name
    # Also mirror the resolved type onto the collision object so it's
    # visible in the Blender UI without re-deriving from the name.
    collision_obj["collision_type_resolved"] = type_word

    return collision_obj


def triangulate_mesh(obj):
    ensure_object_mode()

    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

    triangulate = obj.modifiers.new(
        name="Collision_Triangulate",
        type="TRIANGULATE"
    )

    try:
        bpy.ops.object.modifier_apply(modifier=triangulate.name)
    except RuntimeError as error:
        print(f"Could not triangulate {obj.name}: {error}")


def apply_decimate_safely(collision_obj, ratio, type_word):
    """
    PR-31: never decimate when the user (or auto-detect) declared an
    explicit primitive type.  Decimation can break the topology that
    the C++ classifier relies on — a decimated cube might end up with
    only 5 faces, a decimated wedge might lose the slope normal.  Only
    AUTO / MESH meshes get decimated.
    """
    ensure_object_mode()

    original_face_count = len(collision_obj.data.polygons)

    if type_word not in ("AUTO", "MESH"):
        # Explicit primitive type — leave the mesh exactly as the user
        # authored it.  The C++ loader fits the requested primitive
        # from these triangles directly.
        collision_obj["collision_type"] = type_word
        collision_obj["decimated"] = False
        return True

    if original_face_count <= DO_NOT_DECIMATE_FACE_LIMIT:
        collision_obj["collision_type"] = "TRIMESH_SIMPLE"
        collision_obj["decimated"] = False
        print(
            f"Skipped decimate for {collision_obj.name}: "
            f"simple mesh with {original_face_count} faces."
        )
        return True

    target_faces = int(original_face_count * ratio)

    if target_faces < MIN_COLLISION_FACES:
        safe_ratio = MIN_COLLISION_FACES / original_face_count
    else:
        safe_ratio = ratio

    safe_ratio = min(max(safe_ratio, 0.01), 1.0)

    bpy.ops.object.select_all(action="DESELECT")

    collision_obj.hide_set(False)
    collision_obj.hide_viewport = False
    collision_obj.select_set(True)
    bpy.context.view_layer.objects.active = collision_obj

    decimate = collision_obj.modifiers.new(
        name="Collision_LowPoly_Decimate",
        type="DECIMATE"
    )

    decimate.ratio = safe_ratio
    decimate.use_collapse_triangulate = True

    try:
        bpy.ops.object.modifier_apply(modifier=decimate.name)
    except RuntimeError as error:
        print(f"Could not apply decimate to {collision_obj.name}: {error}")
        return False

    final_face_count = len(collision_obj.data.polygons)

    collision_obj["collision_type"] = "TRIMESH_LOW_POLY"
    collision_obj["decimated"] = True
    collision_obj["decimate_ratio"] = safe_ratio
    collision_obj["original_face_count"] = original_face_count
    collision_obj["final_face_count"] = final_face_count

    return True


def resolve_type_word(source_obj):
    """
    Return the type word the script will write into the COL_ name.

    Precedence (highest first):
      1. Explicit `collision_type` custom property (`get_explicit_type`).
      2. Geometry-based auto-detection (`auto_detect_shape`).
      3. AUTO — emit `COL_<source>` with no type prefix and let the C++
         loader's heuristics decide.
    """
    explicit = get_explicit_type(source_obj)
    if explicit != "AUTO":
        return explicit
    detected = auto_detect_shape(source_obj)
    return detected


def generate_low_poly_collision():
    ensure_object_mode()

    collision_collection = get_or_create_collection(COLLECTION_NAME)

    if CLEAR_OLD_GENERATED_COLLISION:
        clear_old_generated_collision()

    if ONLY_SELECTED:
        source_objects = list(bpy.context.selected_objects)
    else:
        source_objects = list(bpy.context.scene.objects)

    generated = []
    by_type = {}  # for end-of-run summary

    for source_obj in source_objects:
        if not should_generate_collision(source_obj):
            continue

        type_word = resolve_type_word(source_obj)

        collision_obj = duplicate_as_collision_object(
            source_obj,
            collision_collection,
            type_word,
        )

        success = apply_decimate_safely(collision_obj, DECIMATE_RATIO, type_word)

        if not success:
            continue

        # Triangulate AFTER decimation.  For explicit primitives we still
        # triangulate so the GLB exporter doesn't emit n-gons (the C++
        # loader's `aiProcess_Triangulate` would handle that anyway, but
        # baking triangles in Blender keeps the export deterministic).
        triangulate_mesh(collision_obj)

        collision_obj.hide_render = HIDE_COLLISION_RENDER
        collision_obj.hide_viewport = HIDE_COLLISION_VIEWPORT

        if DISPLAY_AS_WIRE:
            collision_obj.display_type = "WIRE"

        generated.append(collision_obj)
        by_type[type_word] = by_type.get(type_word, 0) + 1

        print(
            f"Generated collision: {source_obj.name} → {collision_obj.name} "
            f"[{type_word}] "
            f"({len(collision_obj.data.vertices)} verts, "
            f"{len(collision_obj.data.polygons)} faces)"
        )

    bpy.ops.object.select_all(action="DESELECT")

    for obj in generated:
        obj.select_set(True)

    if generated:
        bpy.context.view_layer.objects.active = generated[0]

    summary = ", ".join(f"{n}× {t}" for t, n in sorted(by_type.items()))
    print(f"Done. Generated {len(generated)} collision meshes — {summary}")

    return generated


# -----------------------------
# Run
# -----------------------------

generate_low_poly_collision()
