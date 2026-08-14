# Readers and asset factories shared by the .uasset bake (pipeline/unreal/bake_map.py).
#
# Everything here runs inside a headless Unreal editor session, so `import unreal` is
# mandatory -- this module is not standalone-importable. The readers are plain Python over
# the export's sidecars, which are already Unreal space (cm, Z-up, left-handed, winding
# reversed at export), so every number is passed through verbatim. The factories wrap the
# editor asset APIs the bake needs: texture import, material instances, static meshes.
import os
import hashlib
import json
import struct

import unreal

from elysium_pipeline import asset_names, shared_corpus

_tools = unreal.AssetToolsHelpers.get_asset_tools()
_mel = unreal.MaterialEditingLibrary
_collision = unreal.GeometryScript_Collision

# Package-name-safe folding lives in elysium_pipeline.asset_names, which imports no `unreal` and
# so is reachable from the offline half too -- the character partition has to name a texture
# asset without an editor. That module also states how this fold differs from the C++
# `FElysiumContentPaths::BakedAssetName`.
safe_name = asset_names.safe_name


def read_glb_json(path):
    """The JSON chunk of a .glb (pipeline/unreal/bake_characters.py and its verifier).

    The character bake reads only the material, texture and image tables -- enough to bind a real
    Texture2D where glTFRuntime bound a transient decode, and enough for the verifier to know which
    materials are supposed to carry an albedo at all. Everything else in the file is glTFRuntime's
    to read; this is deliberately not a glTF parser."""
    with open(path, "rb") as handle:
        magic, _version, _length = struct.unpack("<III", handle.read(12))
        if magic != 0x46546C67:
            raise ValueError("%s is not a .glb" % path)
        chunk_length, _chunk_type = struct.unpack("<II", handle.read(8))
        return json.loads(handle.read(chunk_length).decode("utf-8"))


def glb_bone_parents(glb):
    """{bone name: parent bone name or None} over a .glb's node tree.

    None means the file does not name a parent for that bone -- it is that file's own root -- which
    is NOT the same as asserting the bone is parentless everywhere. A bank is a bare tree rooted at
    Bip01; a body whose VtMB skeleton forks carries a synthetic root above the same Bip01. Reading
    None as "unspecified" is what lets those two describe one rig."""
    nodes = glb.get("nodes", [])
    parent = {}
    for index, node in enumerate(nodes):
        for child in node.get("children", []):
            parent[child] = index
    out = {}
    for index, node in enumerate(nodes):
        name = node.get("name", "")
        if name:
            out[name] = nodes[parent[index]].get("name", "") if index in parent else None
    return out


def rig_trees_compatible(base, tree):
    """Whether `tree` can merge into `base`: every bone they share must agree on its parent, and
    the merge must leave exactly one root.

    Strict about the parent, including the root. A bone that is parentless in one tree and parented
    in the other is a conflict, because USkeleton::MergeBonesToBoneTree rejects exactly that -- a
    model whose VtMB skeleton forks carries a synthetic root above Bip01, and no amount of
    interpretation makes that the same shape as a Bip01-rooted one. Those models get their own
    family, which costs them nothing: a clip binds to a skeleton by BONE NAME, so a bank still
    resolves against the Bip01 subtree inside their skeleton without ever merging into it.

    **Agreeing on every shared bone is not sufficient, because two trees can share nothing.** A prop
    rooted at `Phone_bone_01` conflicts with no bone of a biped family and would merge in, giving
    the skeleton a second parentless bone -- which Unreal's single-rooted reference skeleton
    refuses, at mesh-build time, long after the partition was decided. So the root count is checked
    here rather than discovered there."""
    low_base = _casefold_tree(base)
    low_tree = _casefold_tree(tree)
    if not all(bone not in low_base or low_base[bone] == par
               for bone, par in low_tree.items()):
        return False
    roots = {bone for bone, par in low_base.items() if not par}
    roots |= {bone for bone, par in low_tree.items() if not par}
    return len(roots) <= 1


def _casefold_tree(tree):
    """`tree` keyed and valued by lowercased bone name.

    **`FName` is case-insensitive, so a comparison that is not agrees with Unreal by luck.** The
    generic appendix names are exactly where the cast disagrees about case: `heather` hangs
    `bone01` off `Bip01 Spine1` while `buch` hangs `Bone01` off `Bip01 HeadNub`, which a
    case-sensitive dict reads as two unrelated bones and a skeleton reads as one bone with two
    parents. The partition then hands them to the same family and the mesh build refuses it, long
    after the decision was made and with nothing pointing back at the cause."""
    return {bone.lower(): (par or "").lower() for bone, par in tree.items()}


def rig_tree_merge(base, tree):
    base.update(tree)


def glb_material_albedo(glb):
    """{glTF material name: image uri} for the materials that declare one. A VtMB material carries
    at most one map -- mdl_gltf.py writes either a baseColorTexture or a flat baseColorFactor."""
    images = glb.get("images", [])
    textures = glb.get("textures", [])
    out = {}
    for material in glb.get("materials", []):
        entry = material.get("pbrMetallicRoughness", {}).get("baseColorTexture")
        if entry is None:
            continue
        source = textures[entry["index"]].get("source")
        if source is None:
            continue
        uri = images[source].get("uri", "")
        if uri:
            out[material["name"]] = uri
    return out


# ---------------------------------------------------------------------------- sidecars


class MatDef(object):
    """One OBJ material, mirroring FElysiumMaterialDef so the bake selects the same master
    and binds the same named parameters the runtime factory does."""

    __slots__ = ("name", "material_key", "albedo", "emissive", "bump", "refract_map", "env_mask",
                 "base_tex2", "scissor", "blend", "additive", "glass", "refract",
                 "refract_amount", "env_cube", "env_tint", "wetness_driven",
                 "wetness_scale", "decal", "water", "color")

    # Channel spread above which an $envmaptint counts as CHROMATIC rather than a grey
    # dim-down. The population is bimodal -- 361 of the game's 362 grey tints sit at exactly
    # zero spread and the next value up is 0.05 -- so this separates them with a clear gap on
    # either side rather than splitting a continuum (docs/vtmb/reflections.md).
    CHROMATIC_SPREAD = 0.02

    def __init__(self, name):
        self.name = name
        # The corpus key this surface's definition came from. It is the material's identity, and
        # what the shared material instance is named after; `name` is the surface's own key, which
        # additionally carries the map's cubemap tag.
        self.material_key = name
        self.albedo = ""
        self.emissive = ""
        self.bump = ""
        self.refract_map = ""
        self.env_mask = ""
        self.base_tex2 = ""
        self.scissor = False      # illum 4    -> masked master
        self.blend = False        # blend 1    -> translucent master
        self.additive = False     # additive 1 -> additive master
        self.glass = False        # glass 1    -> UE Thin Translucent glass master
        self.refract = False      # refract N  -> Source framebuffer-distortion master
        self.refract_amount = 0.0 # authored $refractamount, PNO-neutral when zero
        self.env_cube = ""
        self.env_tint = (1.0, 1.0, 1.0)   # envtint -> $envmaptint, white when unauthored
        self.wetness_driven = False
        self.wetness_scale = 0.0
        self.decal = False        # decal 1    -> deferred-decal master
        # water 1 -> a Source water surface. The exporter's flag chain is an if/elif, so a water
        # material is written as `water 1` INSTEAD of `blend 1` and never carries the translucent
        # flag -- reading only `blend` therefore calls every canal, sewer and pier surface opaque.
        self.water = False
        self.color = (0.6, 0.6, 0.65)

    @property
    def opaque(self):
        """True when this surface can carry Nanite (Nanite is opaque/masked only)."""
        return not (self.blend or self.additive or self.refract or self.water)

    @property
    def wet(self):
        """True when this surface actually runs the wetness path.

        `wetness_driven` is the material's authored fact -- its VMT carries GlobalWetness
        proxies. A projected decal bakes onto M_Decal, which carries albedo, self-illum and the
        world fog and nothing else; the wall underneath owns the wetness. So a decal's proxies
        are inert here, and the surface is not counted, fingerprinted or bound as wet."""
        return self.wetness_driven and not self.decal

    @property
    def chromatic(self):
        """True when $envmaptint names a metal: VtMB's own hand-authored metal mask.

        Translucent and additive surfaces are excluded even when their tint is chromatic --
        the blue/teal tints in that population are coloured GLASS, which stays dielectric.
        Metalness is never inferred here; it is read off the game's own authoring."""
        if not self.env_cube or not self.opaque:
            return False
        return max(self.env_tint) - min(self.env_tint) >= self.CHROMATIC_SPREAD

    @property
    def tint_luma(self):
        """The grey half of $envmaptint, as a reflection-strength scale. Rec.709 luma of the
        authored tint; 1.0 when unauthored, so it is neutral by construction."""
        r, g, b = self.env_tint
        return 0.2126 * r + 0.7152 * g + 0.0722 * b


def mat_from_record(name, record, key=""):
    """A `MatDef` from one `shared/materials.json` row.

    Texture fields stay exactly as the record states them -- corpus-relative (`tex/<file>`) -- so
    a caller joins them against the corpus directory, not against a map.
    """
    mat = MatDef(name)
    mat.material_key = key or name
    mat.albedo = record.get("albedo", "")
    mat.emissive = record.get("emissive", "")
    mat.bump = record.get("bump", "")
    mat.refract_map = record.get("refract_map", "")
    mat.env_mask = record.get("env_mask", "")
    mat.base_tex2 = record.get("base_tex2", "")
    mat.scissor = bool(record.get("scissor"))
    mat.blend = bool(record.get("blend"))
    mat.additive = bool(record.get("additive"))
    mat.glass = bool(record.get("glass"))
    mat.refract = bool(record.get("refract"))
    mat.refract_amount = float(record.get("refract_amount") or 0.0)
    mat.env_cube = record.get("env_cube", "")
    tint = record.get("env_tint") or [1.0, 1.0, 1.0]
    mat.env_tint = (float(tint[0]), float(tint[1]), float(tint[2]))
    wetness = record.get("wetness")
    mat.wetness_driven = wetness is not None
    mat.wetness_scale = float(wetness or 0.0)
    mat.decal = bool(record.get("decal"))
    mat.water = bool(record.get("water"))
    return mat


def read_mtl(path, corpus=None, local=None):
    """Parse an exported `.mtl` into `{surface key: MatDef}`.

    The `.mtl` names each surface's material and the two facts only its map holds -- the baked
    cubemap VBSP patched in, and whether the surface is water or a projected decal here. Every
    channel and flag comes from `corpus`, the one definition per material, so two maps cannot
    state different things about one authored material. `local` carries the definitions of
    materials that exist only inside this map's own PAKFILE.

    A surface whose material neither document names is skipped and reported by the caller: a
    silently missing definition would bake as the master's own placeholder.
    """
    corpus = corpus or {}
    local = local or {}
    mats = {}
    if not os.path.isfile(path):
        return mats
    cur = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            tok = line.split()
            if not tok:
                continue
            key = tok[0]
            if key == "newmtl" and len(tok) >= 2:
                cur = tok[1]
            elif cur is None:
                continue
            elif key == "mat" and len(tok) >= 2:
                record = local.get(tok[1]) or corpus.get(tok[1])
                if record is not None:
                    mats[cur] = mat_from_record(cur, record, tok[1])
            elif cur in mats and key == "cube" and len(tok) >= 2:
                # The cube this surface samples is the map's, not the material's.
                mats[cur].env_cube = tok[1]
            elif cur in mats and key == "water" and len(tok) >= 2 and tok[1] == "1":
                mats[cur].water = True
            elif cur in mats and key == "decal" and len(tok) >= 2 and tok[1] == "1":
                mats[cur].decal = True
    return mats


class ObjModel(object):
    """A parsed OBJ: one flat vertex list keyed on the file's own `v/vt` corner tokens, plus
    per-material triangle index triples into it.

    Corners are de-duplicated on the raw token, so vertices stay shared exactly where the
    exporter shared them -- which is what makes recomputed normals come out flat across a BSP
    face boundary and smooth within one, matching the runtime build."""

    __slots__ = ("positions", "uvs", "groups", "mtl_name", "path")

    def __init__(self):
        self.positions = []            # [(x, y, z)]
        self.uvs = []                  # [(u, v)]
        self.groups = {}               # material name -> [i0, i1, i2, ...]
        self.mtl_name = ""
        self.path = ""

    @property
    def tri_count(self):
        return sum(len(v) for v in self.groups.values()) // 3


def read_obj(path):
    """Parse an exported OBJ. Positions and UVs are read verbatim (already Unreal space);
    faces are already triangles with export-reversed winding."""
    model = ObjModel()
    model.path = path
    raw_pos = []
    raw_uv = []
    corners = {}
    cur = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            tok = line.split()
            if not tok:
                continue
            key = tok[0]
            if key == "v":
                raw_pos.append((float(tok[1]), float(tok[2]), float(tok[3])))
            elif key == "vt":
                raw_uv.append((float(tok[1]), float(tok[2])))
            elif key == "usemtl" and len(tok) >= 2:
                cur = model.groups.setdefault(tok[1], [])
            elif key == "mtllib" and len(tok) >= 2:
                model.mtl_name = tok[1]
            elif key == "f":
                if cur is None:
                    cur = model.groups.setdefault("__default", [])
                for token in tok[1:4]:
                    idx = corners.get(token)
                    if idx is None:
                        idx = len(model.positions)
                        corners[token] = idx
                        parts = token.split("/")
                        model.positions.append(raw_pos[int(parts[0]) - 1])
                        if len(parts) > 1 and parts[1]:
                            model.uvs.append(raw_uv[int(parts[1]) - 1])
                        else:
                            model.uvs.append((0.0, 0.0))
                    cur.append(idx)
    return model


class DecalDef(object):
    """One projected decal from a `.decals` line, mirroring FElysiumDecalDef. Every vector is
    Unreal space already, so the placer reads them verbatim: `normal` is the room-facing
    projection axis, `s_dir`/`t_dir` the surface tangent frame, `half_w`/`half_h` the
    on-surface half-extents in cm. `mat` keys into the shared `<map>.mtl`."""

    __slots__ = ("mat", "loc", "normal", "s_dir", "t_dir", "half_w", "half_h")


def read_decals(path):
    """Parse a `.decals` sidecar into [DecalDef]:
       `<material> lx ly lz  nx ny nz  sx sy sz  tx ty tz  hw hh`
    15 whitespace-separated tokens; malformed lines are skipped."""
    out = []
    if not os.path.isfile(path):
        return out
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            tok = line.split()
            if len(tok) != 15:
                continue
            value = [float(t) for t in tok[1:]]
            decal = DecalDef()
            decal.mat = tok[0]
            decal.loc = unreal.Vector(value[0], value[1], value[2])
            decal.normal = unreal.Vector(value[3], value[4], value[5])
            decal.s_dir = unreal.Vector(value[6], value[7], value[8])
            decal.t_dir = unreal.Vector(value[9], value[10], value[11])
            decal.half_w = value[12]
            decal.half_h = value[13]
            out.append(decal)
    return out


def read_skins(path):
    """Parse a `props/<stem>.skins` sidecar into {family index: {authored material: family
    material}}:
        `<family> <authored>=<family> [...]`
    One line per alternate skin family that repaints something; family 0 (the authored set) is
    never written, and a family that repaints nothing is simply absent. Missing file -> {}."""
    out = {}
    if not os.path.isfile(path):
        return out
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            tok = line.split()
            if len(tok) < 2 or not tok[0].isdigit():
                continue
            remap = {}
            for pair in tok[1:]:
                if "=" in pair:
                    base, rep = pair.split("=", 1)
                    remap[base] = rep
            if remap:
                out[int(tok[0])] = remap
    return out


def read_phys(path):
    """Parse a `props/<stem>.phys` sidecar -- VtMB's own VPhysics collision, decoded by
    `phy.py` -- into {"mass": kg, "hulls": [(verts, tris), ...]}:
        mass <kg>
        hull <x y z x y z ...>      flat Unreal-cm verts, one convex hull
        tris <i j k i j k ...>      its triangles, indexing that hull's verts
    Two lines per hull, in that order. Missing file -> None, which is the runtime's signal
    that the model carries no collision at all."""
    if not os.path.isfile(path):
        return None
    mass, hulls, pending = 0.0, [], None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            tok = line.split()
            if not tok:
                continue
            if tok[0] == "mass" and len(tok) > 1:
                mass = float(tok[1])
            elif tok[0] == "hull":
                nums = [float(v) for v in tok[1:]]
                pending = [tuple(nums[i:i + 3]) for i in range(0, len(nums) - 2, 3)]
            elif tok[0] == "tris" and pending is not None:
                idx = [int(v) for v in tok[1:]]
                hulls.append((pending, [tuple(idx[i:i + 3])
                                        for i in range(0, len(idx) - 2, 3)]))
                pending = None
    return {"mass": mass, "hulls": hulls}


def read_floats(path):
    """One float per line (the `.blend` per-vertex WorldVertexTransition weight sidecar)."""
    if not os.path.isfile(path):
        return []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return [float(line) for line in handle if line.strip()]


def vertex_normals(positions, index_lists):
    """Area-weighted vertex normals accumulated over the model's own shared indices -- the
    same thing FStaticMeshOperations::ComputeTangentsAndNormals produces on the runtime
    build, so shading matches. Returns a list parallel to `positions`.

    The winding is Unreal's: `source_to_unreal` negates Y, which is a reflection, so the
    exporter reverses triangle winding at OBJ-write time. The outward normal is therefore
    (c - a) x (b - a), not the right-handed (b - a) x (c - a) -- with the latter every
    triangle on the map's floor plane points straight down."""
    acc = [[0.0, 0.0, 0.0] for _ in positions]
    for indices in index_lists:
        for base in range(0, len(indices), 3):
            i0, i1, i2 = indices[base], indices[base + 1], indices[base + 2]
            ax, ay, az = positions[i0]
            bx, by, bz = positions[i1]
            cx, cy, cz = positions[i2]
            ux, uy, uz = cx - ax, cy - ay, cz - az
            vx, vy, vz = bx - ax, by - ay, bz - az
            # Cross product magnitude is twice the triangle area, so an unnormalized
            # accumulation is already area-weighted.
            nx = uy * vz - uz * vy
            ny = uz * vx - ux * vz
            nz = ux * vy - uy * vx
            for i in (i0, i1, i2):
                slot = acc[i]
                slot[0] += nx
                slot[1] += ny
                slot[2] += nz
    out = []
    for nx, ny, nz in acc:
        length = (nx * nx + ny * ny + nz * nz) ** 0.5
        if length > 1e-12:
            out.append((nx / length, ny / length, nz / length))
        else:
            out.append((0.0, 0.0, 1.0))
    return out


# ---------------------------------------------------------------------------- assets


def ensure_dir(package):
    if not unreal.EditorAssetLibrary.does_directory_exist(package):
        unreal.EditorAssetLibrary.make_directory(package)


def import_textures(jobs, package):
    """Batch-import texture source files. `jobs` is [(abs_path, asset_name)]; returns
    {asset_name: Texture2D}. Existing generated assets are replaced in place so a changed
    source PNG cannot leave stale pixel data behind while retaining its package identity."""
    ensure_dir(package)
    tasks = []
    have = {}
    for src, name in jobs:
        task = unreal.AssetImportTask()
        task.filename = src
        task.destination_path = package
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.replace_existing_settings = True
        task.save = False
        tasks.append(task)
    if tasks:
        _tools.import_asset_tasks(tasks)
        for task in tasks:
            target = "%s/%s" % (package, task.destination_name)
            asset = unreal.EditorAssetLibrary.load_asset(target)
            if asset:
                have[task.destination_name] = asset
    return have


def file_md5(path):
    digest = hashlib.md5()
    with open(path, "rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def asset_tag(asset_path, name):
    data = unreal.EditorAssetLibrary.find_asset_data(asset_path)
    result = unreal.AssetRegistryHelpers.get_tag_value(data, name)
    if isinstance(result, tuple):
        if len(result) == 2 and isinstance(result[0], bool):
            return str(result[1]) if result[0] else ""
        return str(result[-1]) if result else ""
    return str(result) if result is not None else ""


def asset_class_name(asset_path):
    """Read the registry class without loading the package into the editor process."""
    data = unreal.EditorAssetLibrary.find_asset_data(asset_path)
    try:
        return str(data.asset_class_path.asset_name)
    except (AttributeError, TypeError):
        return ""


def texture_source_md5(asset_path):
    """Return Unreal's stored source MD5 from the hidden SourceFile registry tag."""
    try:
        rows = json.loads(asset_tag(asset_path, "SourceFile"))
    except (TypeError, ValueError):
        return ""
    if not isinstance(rows, list) or len(rows) != 1 or not isinstance(rows[0], dict):
        return ""
    return str(rows[0].get("FileMD5", "")).lower()


def configure_texture(texture, role):
    """Set the compression/colour-space a texture's role needs. `role` is one of
    'albedo' (sRGB colour + alpha), 'normal' (tangent-space bump), 'mask' (linear
    single-channel reflectivity), 'cube' (sRGB source reflection), or 'height'
    (linear 16-bit rain-cover height)."""
    if role == "height":
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings",
                                    unreal.TextureCompressionSettings.TC_DISPLACEMENTMAP)
        texture.set_editor_property("mip_gen_settings",
                                    unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    elif role == "normal":
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings",
                                    unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif role == "mask":
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings",
                                    unreal.TextureCompressionSettings.TC_MASKS)
    else:
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("compression_settings",
                                    unreal.TextureCompressionSettings.TC_DEFAULT)


def make_material_instance(name, package, parent):
    """A MaterialInstanceConstant parented to one of the generated masters. An existing asset
    is reused with its parameters cleared, so a re-run re-authors it in place rather than
    fighting the unattended overwrite prompt."""
    ensure_dir(package)
    target = "%s/%s" % (package, name)
    if unreal.EditorAssetLibrary.does_asset_exist(target):
        mic = unreal.EditorAssetLibrary.load_asset(target)
        if mic:
            _mel.clear_all_material_instance_parameters(mic)
    else:
        mic = _tools.create_asset(name, package, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
    if mic:
        _mel.set_material_instance_parent(mic, parent)
    return mic


def make_skin_set(name, package, models):
    """Author the map's UElysiumPropSkinSet -- the prop-skin table the runtime binds.

    `models` is [(stem, [family_overrides])] where family_overrides is a list indexed by skin
    family, each a {slot name: UMaterialInterface} (empty for family 0 and for any family that
    repaints nothing). Storing real material objects is the point: they are hard references, so
    every alternate material stays reachable from the level and binds at exactly the quality the
    authored one does.

    Resolved by load-first, then create: does_asset_exist reports False for an asset already on
    this mount even after a forced rescan, and create_asset then trips the unattended overwrite
    guard. The whole Models array is overwritten, because a bake that died mid-run leaves a
    half-populated asset behind. The load goes through `unreal.load_asset` (LoadObject) rather
    than the EditorAssetLibrary one, which logs a hard *Error* when the registry has no such
    asset -- on the first bake of a map that is the normal path, and it would make a clean run
    report failure."""
    ensure_dir(package)
    target = "%s/%s" % (package, name)
    asset = unreal.load_asset(target)
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.ElysiumPropSkinSet)
        asset = _tools.create_asset(name, package, unreal.ElysiumPropSkinSet, factory)
    if asset is None:
        return None

    entries = []
    for stem, families in models:
        rows = []
        for overrides in families:
            # A USTRUCT's generated Python type takes no constructor kwargs unless its
            # properties are Blueprint-exposed, so every field goes in by set_editor_property.
            row = unreal.ElysiumSkinFamily()
            items = []
            for slot, material in sorted(overrides.items()):
                item = unreal.ElysiumSkinOverride()
                item.set_editor_property("slot_name", slot)
                item.set_editor_property("material", material)
                items.append(item)
            row.set_editor_property("overrides", items)
            rows.append(row)
        entry = unreal.ElysiumPropSkinModel()
        entry.set_editor_property("stem", stem)
        entry.set_editor_property("families", rows)
        entries.append(entry)
    asset.set_editor_property("models", entries)
    return asset


def _assert_prunable(package, scope):
    """Refuse to prune the shared corpus from a scope that does not author all of it.

    Every map references the corpus packages, and a map -- or a single-unit run -- knows only the
    handful of assets it wanted. Pruning one of those packages against that wanted set would
    delete the install's textures, materials and meshes for every other map. Only the whole-corpus
    pass, which authors the complete wanted set from `manifest.json`, may state this scope.
    """
    if package.startswith(shared_corpus.BAKED_ROOT) and scope != shared_corpus.SCOPE:
        raise RuntimeError(
            "refusing to prune the shared corpus package %s from scope %r" % (package, scope))


def prune_package(package, keep, scope=""):
    """Delete every asset directly in `package` whose object name is not in `keep`. Only safe
    for a package one stage owns outright and re-authors in full. Returns the number deleted."""
    _assert_prunable(package, scope)
    if not unreal.EditorAssetLibrary.does_directory_exist(package):
        return 0
    gone = 0
    for path in unreal.EditorAssetLibrary.list_assets(package, recursive=False,
                                                      include_folder=False):
        name = path.rsplit("/", 1)[-1].split(".")[0]
        if name in keep:
            continue
        delete_owned_asset(path)
        gone += 1
    return gone


def prune_package_prefix(package, prefix, keep, scope=""):
    """Delete directly-owned assets whose object names start with `prefix` and are not in
    `keep`. Use this when several stages share one package but own disjoint name families."""
    _assert_prunable(package, scope)
    if not unreal.EditorAssetLibrary.does_directory_exist(package):
        return 0
    gone = 0
    for path in unreal.EditorAssetLibrary.list_assets(package, recursive=False,
                                                      include_folder=False):
        name = path.rsplit("/", 1)[-1].split(".")[0]
        if not name.startswith(prefix) or name in keep:
            continue
        delete_owned_asset(path)
        gone += 1
    return gone


def delete_owned_asset(asset_path):
    """Delete a generated asset or fail the commandlet; failed pruning is not cacheable."""
    if not unreal.EditorAssetLibrary.delete_asset(asset_path):
        raise RuntimeError("could not delete generated asset: %s" % asset_path)


def set_tex_param(mic, param, texture):
    _mel.set_material_instance_texture_parameter_value(mic, param, texture)


def set_scalar_param(mic, param, value):
    _mel.set_material_instance_scalar_parameter_value(mic, param, value)


def set_vector_param(mic, param, value):
    _mel.set_material_instance_vector_parameter_value(mic, param, value)


def set_static_switch_param(mic, param, value):
    _mel.set_material_instance_static_switch_parameter_value(mic, param, value)


def build_dynamic_mesh(sections):
    """Build one UDynamicMesh from `sections` = [(positions, normals, uvs, colors, tris)],
    each appended under its own material id (its index in the list)."""
    mesh = unreal.DynamicMesh()
    for slot, (positions, normals, uvs, colors, tris) in enumerate(sections):
        buffers = unreal.GeometryScriptSimpleMeshBuffers()
        buffers.vertices = [unreal.Vector(p[0], p[1], p[2]) for p in positions]
        buffers.normals = [unreal.Vector(n[0], n[1], n[2]) for n in normals]
        buffers.uv0 = [unreal.Vector2D(t[0], t[1]) for t in uvs]
        if colors:
            buffers.vertex_colors = [unreal.LinearColor(c, c, c, 1.0) for c in colors]
        buffers.triangles = [unreal.IntVector(tris[i], tris[i + 1], tris[i + 2])
                             for i in range(0, len(tris), 3)]
        result = unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(
            mesh, buffers, material_id=slot, defer_change_notifications=True)
        # The node returns (TargetMesh, NewTriangleIndicesList); older signatures return the
        # mesh alone.
        mesh = result[0] if isinstance(result, tuple) else result
    return mesh


def mesh_triangle_count(mesh):
    """Triangles the UDynamicMesh actually holds. FDynamicMesh3 refuses any triangle that
    would make the topology non-manifold, so this is the count that must be checked against
    the source -- a rejection is only a log line otherwise."""
    return unreal.GeometryScript_MeshQueries.get_num_triangle_i_ds(mesh)


def set_complex_collision(static_mesh):
    """Make the render triangles themselves the mesh's collision.

    Two things need this. A solid GAME_LUMP prop has to block the pawn, and the trimesh is a
    truer blocker than the single convex hull the runtime builder cooked. And the debug
    click-pick wants a face index it can turn back into a material slot, which simple collision
    cannot give. Safe for every baked mesh because none of them simulate -- prop_physics goes
    through the runtime path, which needs a body setup of its own anyway.

    World and sky geometry carries this too but ignores every channel except the pick one, so
    walking is still decided entirely by the .hulls brush collider."""
    body = static_mesh.get_editor_property("body_setup")
    if body is None:
        static_mesh.create_body_setup()
        body = static_mesh.get_editor_property("body_setup")
    if body is not None:
        body.set_editor_property(
            "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)


def set_phy_collision(static_mesh, phys):
    """Give a physics prop the collision VtMB simulates against: one convex shape per `.phy`
    ledge, plus the model's authored mass. Returns the shape count.

    Each ledge is already convex (`phy.is_convex` asserts it at export), so it is handed to
    the hull builder one at a time with `max_convex_hulls_per_mesh = 1` and simplification
    off -- that reproduces the hull exactly rather than decomposing or approximating it.

    `CTF_UseSimpleAndComplex` rather than the `CTF_UseComplexAsSimple` every other baked mesh
    carries: a Chaos rigid body can only simulate against *simple* shapes, while the debug
    pick still wants a per-poly face index (it traces with bTraceComplex). Both shapes get
    cooked, so the same asset serves a simulating prop and a static placement of the same
    model.

    Mass rides the body setup's default instance, so it is asset data -- no runtime sidecar
    read, and it survives the .umap save/load. The entity's own `override_mass` key still
    outranks it at spawn, which is Source's precedence."""
    opts = unreal.GeometryScriptCollisionFromMeshOptions()
    opts.set_editor_property(
        "method", unreal.GeometryScriptCollisionGenerationMethod.CONVEX_HULLS)
    opts.set_editor_property("max_convex_hulls_per_mesh", 1)
    opts.set_editor_property("simplify_hulls", False)
    opts.set_editor_property("emit_transaction", False)

    parts = []
    for verts, tris in phys["hulls"]:
        buffers = unreal.GeometryScriptSimpleMeshBuffers()
        buffers.vertices = [unreal.Vector(v[0], v[1], v[2]) for v in verts]
        buffers.triangles = [unreal.IntVector(t[0], t[1], t[2]) for t in tris]
        hull = unreal.DynamicMesh()
        result = unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(
            hull, buffers, material_id=0)
        hull = result[0] if isinstance(result, tuple) else result
        parts.append(_collision.generate_collision_from_mesh(hull, opts))
    if not parts:
        return 0

    combined = _collision.combine_simple_collision_array(parts)
    if isinstance(combined, tuple):
        combined = combined[0]
    _collision.set_simple_collision_of_static_mesh(
        combined, static_mesh, unreal.GeometryScriptSetSimpleCollisionOptions(),
        unreal.GeometryScriptSetStaticMeshCollisionOptions())

    body = static_mesh.get_editor_property("body_setup")
    if body is not None:
        body.set_editor_property(
            "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX)
        if phys["mass"] > 0.0:
            instance = body.get_editor_property("default_instance")
            instance.set_editor_property("override_mass", True)
            instance.set_editor_property("mass_in_kg_override", phys["mass"])
            body.set_editor_property("default_instance", instance)
    return _collision.get_simple_collision_shape_count(combined)


def create_static_mesh(mesh, asset_path, materials, slot_names, nanite, collision=True):
    """Write a UDynamicMesh out as a real StaticMesh asset and bind its material slots.
    Returns the asset, or None when the build failed."""
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        delete_owned_asset(asset_path)
    nanite_settings = unreal.MeshNaniteSettings()
    nanite_settings.set_editor_property("enabled", nanite)
    options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    options.enable_recompute_normals = False
    options.enable_recompute_tangents = True
    options.enable_nanite = nanite
    options.nanite_settings = nanite_settings
    # A body setup has to exist before either collision helper can touch it. Runtime brush
    # visuals deliberately opt out: their separately cooked entity hull is authoritative.
    options.enable_collision = collision
    # BSP soup is non-manifold; keeping the source vertex order stops the build from welding
    # face-boundary corners back together and smoothing the flat shading away.
    options.use_original_vertex_order = True
    result = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(
        mesh, asset_path, options)
    static_mesh = result[0] if isinstance(result, tuple) else result
    if not static_mesh:
        return None
    # The creation option alone does not land on the asset, so set the settings struct on the
    # mesh as well; assigning it runs PostEditChange, which rebuilds with Nanite.
    static_mesh.set_editor_property("nanite_settings", nanite_settings)
    static_mesh.set_editor_property("static_materials", [
        unreal.StaticMaterial(material_interface=mat, material_slot_name=name)
        for mat, name in zip(materials, slot_names)])
    return static_mesh


def save(asset_path):
    return unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=True)
