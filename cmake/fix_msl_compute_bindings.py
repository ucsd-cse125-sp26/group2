#!/usr/bin/env python3
"""Fix MSL shader resource binding indices to match SDL3 GPU's Metal backend.

SDL3 GPU Metal backend binds resources in this order (for ALL stages):

  Textures:
    [0 .. S-1]                              sampler textures
    [S .. S+RO-1]                           read-only storage textures
    [S+RO .. S+RO+RW-1]                    read-write storage textures

  Buffers:
    [0 .. U-1]                              uniform buffers
    [U .. U+ROB-1]                          read-only storage buffers
    [U+ROB .. U+ROB+RWB-1]                 read-write storage buffers

  Samplers:
    [0 .. S-1]                              samplers (same count as sampler textures)

Within each category, items are ordered by their SPIR-V binding number.

Usage:  fix_msl_bindings.py <spirv-cross-path> <spv-file> <msl-file>
"""

import json
import re
import subprocess
import sys


def get_reflection(spirv_cross: str, spv_path: str) -> dict:
    result = subprocess.run(
        [spirv_cross, spv_path, "--reflect"],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"spirv-cross --reflect failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)
    return json.loads(result.stdout)


def build_sampler_binding_map(reflection: dict) -> dict:
    """Map from variable name → SPIR-V binding for sampler textures (combined image/samplers)."""
    m = {}
    for tex in reflection.get("textures", []):
        m[tex["name"]] = tex["binding"]
    return m


def build_image_binding_map(reflection: dict) -> dict:
    """Map from variable name → SPIR-V binding for storage images."""
    m = {}
    for img in reflection.get("images", []):
        m[img["name"]] = img["binding"]
    return m


def build_ubo_binding_map(reflection: dict) -> dict:
    """Map from UBO type/block name → SPIR-V binding."""
    m = {}
    for ubo in reflection.get("ubos", []):
        m[ubo["name"]] = ubo["binding"]
    return m


def build_ssbo_binding_map(reflection: dict) -> dict:
    """Map from SSBO name → (binding, readonly)."""
    m = {}
    for ssbo in reflection.get("ssbos", []):
        m[ssbo["name"]] = ssbo["binding"]
    return m


def classify_param(param_text: str) -> str:
    s = param_text.strip()

    # Read-write storage texture: has access::write or access::read_write
    if ("access::write" in s or "access::read_write" in s) and "[[texture(" in s:
        return "rw_storage_texture"

    # Read-only storage texture: has access::read (but NOT read_write)
    if "access::read" in s and "access::read_write" not in s and "[[texture(" in s:
        return "ro_storage_texture"

    # Sampler object
    if re.match(r"^\s*sampler\s+", s):
        return "sampler"

    # Storage buffer (device pointer/reference)
    if ("device " in s or "const device " in s) and "[[buffer(" in s:
        if "const device " in s:
            return "ro_storage_buffer"
        return "rw_storage_buffer"

    # Uniform buffer (constant reference)
    if s.lstrip().startswith("constant ") and "[[buffer(" in s:
        return "uniform_buffer"

    # Sampler texture (no access qualifier, has [[texture(]])
    if "[[texture(" in s:
        return "sampler_texture"

    return "builtin"


def extract_name(param_text: str) -> str:
    """Extract the variable name from a parameter declaration."""
    s = param_text.strip()
    # Remove [[ ... ]] attributes
    s = re.sub(r"\[\[.*?\]\]", "", s).strip().rstrip(",")
    tokens = s.split()
    if not tokens:
        return ""
    name = tokens[-1].rstrip("&*")
    return name


def extract_type_name(param_text: str) -> str:
    """Extract the TYPE name from a buffer parameter (for matching against SPIR-V reflection).

    SPIR-V reflection uses block/type names ('Material', 'ShadowData', 'GTAOParams'),
    not the MSL variable names ('mat', 'shadow', '_44').
    """
    s = param_text.strip()
    # Remove attributes
    s = re.sub(r"\[\[.*?\]\]", "", s).strip()
    # "constant TypeName& varName" → TypeName
    # "const device TypeName& varName" → TypeName
    # "const device TypeName* varName" → TypeName
    # "device TypeName& varName" → TypeName
    m = re.match(r"(?:const\s+)?(?:device|constant)\s+(\w+)", s)
    if m:
        return m.group(1)
    return ""


def fix_msl_bindings(src: str, reflection: dict) -> str:
    # Match function signature: vertex/fragment/kernel
    pattern = re.compile(
        r"((?:vertex|fragment|kernel)\s+\w+\s+main0\s*\()(.+?)(\)\s*\n)",
        re.DOTALL,
    )
    m = pattern.search(src)
    if not m:
        return src

    prefix = m.group(1)
    params_str = m.group(2)
    suffix = m.group(3)

    # Split parameters respecting nested <> and ()
    params = []
    depth = 0
    current = []
    for ch in params_str:
        if ch in "<(":
            depth += 1
            current.append(ch)
        elif ch in ">)":
            depth -= 1
            current.append(ch)
        elif ch == "," and depth == 0:
            params.append("".join(current))
            current = []
        else:
            current.append(ch)
    if current:
        params.append("".join(current))

    classified = [(p, classify_param(p)) for p in params]

    sampler_map = build_sampler_binding_map(reflection)
    image_map = build_image_binding_map(reflection)
    ubo_map = build_ubo_binding_map(reflection)
    ssbo_map = build_ssbo_binding_map(reflection)

    def sort_key_by_name(item, binding_map):
        """Sort using variable name (for textures/samplers where MSL name matches reflection)."""
        name = extract_name(item[0])
        return binding_map.get(name, 999)

    def sort_key_by_type(item, binding_map):
        """Sort using type name (for buffers where reflection uses block/type name, not variable name)."""
        type_name = extract_type_name(item[0])
        return binding_map.get(type_name, 999)

    # Group by category
    sampler_textures = [(p, c) for p, c in classified if c == "sampler_texture"]
    ro_storage_textures = [(p, c) for p, c in classified if c == "ro_storage_texture"]
    rw_storage_textures = [(p, c) for p, c in classified if c == "rw_storage_texture"]
    samplers = [(p, c) for p, c in classified if c == "sampler"]
    uniform_buffers = [(p, c) for p, c in classified if c == "uniform_buffer"]
    ro_storage_buffers = [(p, c) for p, c in classified if c == "ro_storage_buffer"]
    rw_storage_buffers = [(p, c) for p, c in classified if c == "rw_storage_buffer"]
    builtins = [(p, c) for p, c in classified if c == "builtin"]

    # Sort each group by SPIR-V binding number.
    # Textures/samplers: match by variable name (MSL name == reflection name).
    # Buffers: match by TYPE name (reflection uses block name, not variable name).
    sampler_textures.sort(key=lambda x: sort_key_by_name(x, sampler_map))
    ro_storage_textures.sort(key=lambda x: sort_key_by_name(x, image_map))
    rw_storage_textures.sort(key=lambda x: sort_key_by_name(x, image_map))
    uniform_buffers.sort(key=lambda x: sort_key_by_type(x, ubo_map))
    ro_storage_buffers.sort(key=lambda x: sort_key_by_type(x, ssbo_map))
    rw_storage_buffers.sort(key=lambda x: sort_key_by_type(x, ssbo_map))

    def sampler_sort_key(item):
        name = extract_name(item[0]).replace("Smplr", "")
        return sampler_map.get(name, 999)

    samplers.sort(key=sampler_sort_key)

    # Assign texture indices: samplers first, then RO storage, then RW storage
    new_params = []
    tex_idx = 0
    for p, c in sampler_textures:
        p = re.sub(r"\[\[texture\(\d+\)\]\]", f"[[texture({tex_idx})]]", p)
        new_params.append(p)
        tex_idx += 1
    for p, c in ro_storage_textures:
        p = re.sub(r"\[\[texture\(\d+\)\]\]", f"[[texture({tex_idx})]]", p)
        new_params.append(p)
        tex_idx += 1
    for p, c in rw_storage_textures:
        p = re.sub(r"\[\[texture\(\d+\)\]\]", f"[[texture({tex_idx})]]", p)
        new_params.append(p)
        tex_idx += 1

    # Assign sampler indices: sequential from 0
    for i, (p, c) in enumerate(samplers):
        p = re.sub(r"\[\[sampler\(\d+\)\]\]", f"[[sampler({i})]]", p)
        new_params.append(p)

    # Assign buffer indices: UBOs first, then RO storage, then RW storage
    buf_idx = 0
    for p, c in uniform_buffers:
        p = re.sub(r"\[\[buffer\(\d+\)\]\]", f"[[buffer({buf_idx})]]", p)
        new_params.append(p)
        buf_idx += 1
    for p, c in ro_storage_buffers:
        p = re.sub(r"\[\[buffer\(\d+\)\]\]", f"[[buffer({buf_idx})]]", p)
        new_params.append(p)
        buf_idx += 1
    for p, c in rw_storage_buffers:
        p = re.sub(r"\[\[buffer\(\d+\)\]\]", f"[[buffer({buf_idx})]]", p)
        new_params.append(p)
        buf_idx += 1

    # Builtins (stage_in, vertex_id, thread_position_in_grid, etc.)
    for p, c in builtins:
        new_params.append(p)

    new_params_str = ", ".join(new_params)
    return src[: m.start()] + prefix + new_params_str + suffix + src[m.end() :]


def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <spirv-cross> <spv-file> <msl-file>", file=sys.stderr)
        sys.exit(1)

    spirv_cross, spv_path, msl_path = sys.argv[1], sys.argv[2], sys.argv[3]
    reflection = get_reflection(spirv_cross, spv_path)

    with open(msl_path, "r") as f:
        src = f.read()

    fixed = fix_msl_bindings(src, reflection)

    with open(msl_path, "w") as f:
        f.write(fixed)


if __name__ == "__main__":
    main()
