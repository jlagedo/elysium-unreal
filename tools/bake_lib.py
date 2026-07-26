# Readers and asset factories shared by the .uasset bake (tools/bake_map.py).
#
# Everything here runs inside a headless Unreal editor session, so `import unreal` is
# mandatory -- this module is not standalone-importable. The readers are plain Python over
# the export's sidecars, which are already Unreal space (cm, Z-up, left-handed, winding
# reversed at export), so every number is passed through verbatim. The factories wrap the
# editor asset APIs the bake needs: texture import, material instances, static meshes.
import os
import re

import unreal

_tools = unreal.AssetToolsHelpers.get_asset_tools()
_mel = unreal.MaterialEditingLibrary
_collision = unreal.GeometryScript_Collision

# Package-name-safe: Unreal object names allow letters, digits and underscore.
_UNSAFE = re.compile(r"[^A-Za-z0-9_]+")


def safe_name(text):
    """An OBJ material / texture path turned into a legal Unreal object name."""
    return _UNSAFE.sub("_", text).strip("_") or "unnamed"


# ---------------------------------------------------------------------------- sidecars


class MatDef(object):
    """One OBJ material, mirroring FElysiumMaterialDef so the bake selects the same master
    and binds the same named parameters the runtime factory does."""

    __slots__ = ("name", "albedo", "emissive", "bump", "env_mask", "base_tex2",
                 "scissor", "blend", "additive", "envmap", "decal", "color")

    def __init__(self, name):
        self.name = name
        self.albedo = ""
        self.emissive = ""
        self.bump = ""
        self.env_mask = ""
        self.base_tex2 = ""
        self.scissor = False      # illum 4    -> masked master
        self.blend = False        # blend 1    -> translucent master
        self.additive = False     # additive 1 -> additive master
        self.envmap = False
        self.decal = False        # decal 1    -> deferred-decal master
        self.color = (0.6, 0.6, 0.65)

    @property
    def opaque(self):
        """True when this surface can carry Nanite (Nanite is opaque/masked only)."""
        return not (self.blend or self.additive)


def read_mtl(path):
    """Parse an exported .mtl into {name: MatDef}. Mirrors FElysiumObjModel::ParseMtlLines."""
    mats = {}
    cur = None
    if not os.path.isfile(path):
        return mats
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            tok = line.split()
            if not tok:
                continue
            key = tok[0]
            if key == "newmtl" and len(tok) >= 2:
                cur = MatDef(tok[1])
                mats[tok[1]] = cur
            elif cur is None:
                continue
            elif key == "map_Kd" and len(tok) >= 2:
                cur.albedo = tok[1]
            elif key == "map_Ke" and len(tok) >= 2:
                cur.emissive = tok[1]
            elif key == "illum" and len(tok) >= 2 and tok[1] == "4":
                cur.scissor = True
            elif key == "blend" and len(tok) >= 2 and tok[1] == "1":
                cur.blend = True
            elif key == "additive" and len(tok) >= 2 and tok[1] == "1":
                cur.additive = True
            elif key == "decal" and len(tok) >= 2 and tok[1] == "1":
                cur.decal = True
            elif key == "bumpmap" and len(tok) >= 2:
                cur.bump = tok[1]
            elif key == "envmapmask" and len(tok) >= 2:
                cur.env_mask = tok[1]
            elif key == "envmap" and len(tok) >= 2:
                cur.envmap = True
            elif key == "basetex2" and len(tok) >= 2:
                cur.base_tex2 = tok[1]
            elif key == "Kd" and len(tok) >= 4:
                cur.color = (float(tok[1]), float(tok[2]), float(tok[3]))
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
    {asset_name: Texture2D}. Existing assets are reused, so a re-run is cheap."""
    ensure_dir(package)
    tasks = []
    have = {}
    for src, name in jobs:
        target = "%s/%s" % (package, name)
        if unreal.EditorAssetLibrary.does_asset_exist(target):
            have[name] = unreal.EditorAssetLibrary.load_asset(target)
            continue
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


def configure_texture(texture, role):
    """Set the compression/colour-space a texture's role needs. `role` is one of
    'albedo' (sRGB colour + alpha), 'normal' (tangent-space bump) or 'mask' (linear
    single-channel reflectivity)."""
    if role == "normal":
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
    """A MaterialInstanceConstant parented to one of the committed masters. An existing asset
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


def prune_package(package, keep):
    """Delete every asset directly in `package` whose object name is not in `keep`. Only safe
    for a package one stage owns outright and re-authors in full. Returns the number deleted."""
    if not unreal.EditorAssetLibrary.does_directory_exist(package):
        return 0
    gone = 0
    for path in unreal.EditorAssetLibrary.list_assets(package, recursive=False,
                                                      include_folder=False):
        name = path.rsplit("/", 1)[-1].split(".")[0]
        if name in keep:
            continue
        if unreal.EditorAssetLibrary.delete_asset(path):
            gone += 1
    return gone


def set_tex_param(mic, param, texture):
    _mel.set_material_instance_texture_parameter_value(mic, param, texture)


def set_scalar_param(mic, param, value):
    _mel.set_material_instance_scalar_parameter_value(mic, param, value)


def set_vector_param(mic, param, value):
    _mel.set_material_instance_vector_parameter_value(mic, param, value)


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


def create_static_mesh(mesh, asset_path, materials, slot_names, nanite):
    """Write a UDynamicMesh out as a real StaticMesh asset and bind its material slots.
    Returns the asset, or None when the build failed."""
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.EditorAssetLibrary.delete_asset(asset_path)
    nanite_settings = unreal.MeshNaniteSettings()
    nanite_settings.set_editor_property("enabled", nanite)
    options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    options.enable_recompute_normals = False
    options.enable_recompute_tangents = True
    options.enable_nanite = nanite
    options.nanite_settings = nanite_settings
    # A body setup has to exist before either collision helper above can touch it.
    options.enable_collision = True
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
    return unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
