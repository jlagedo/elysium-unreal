"""Phase 2 of `uv run elysium import models`: land the staged model corpus as Unreal
`UStaticMesh` assets below `/ElysiumBaked/Models/<dir>/SM_<base>`.

Runs inside a headless editor (`-run=pythonscript -script=pipeline/unreal/import_models.py
-ImportModels=<manifest.json> -ImportUnitRoot=<export_v2 root>`). The offline stage
(`importers/models.py`, R1.3) already decided everything the contract calls a decision -- asset
path, slot list and slot names, the material each slot binds, every skin family, which LOD rows
are built and at what `ScreenSize`, the collision mode, the Nanite verdict, the physical material
-- and wrote it to the manifest and one provenance sidecar per unit. This script only executes
those decisions against the editor, and reads the unit's own GLB for the one thing the manifest
cannot carry: the geometry (`docs/architecture/seam_map_model.md` -> "Import").

  * per entry, compare the manifest recipe against the stamp the asset carries
    (`bake_lib.RECIPE_TAG`) and touch only what is new, changed or forced;
  * read the GLB accessors LOD by LOD -- `POSITION`, `NORMAL`, `TEXCOORD_0`, `TANGENT` when the
    source stores it, `UNSIGNED_INT` indices -- through the contract's one coordinate rule
    (`(X,Y,Z)_ue = (x,z,y)_gltf * 100`, directions unscaled and re-normalized, `TANGENT.w`
    negated, winding reversed exactly once), keeping submodel 0 of each body part;
  * author LOD 0 plus every adopted VTX LOD, one mesh section per primitive, each section's
    material id being its slot's position in the stage's slot list, so `GetMaterialIndex(SlotName)`
    resolves on the disambiguated names the skin table also uses;
  * cook collision: one convex shape per PHY ledge (unsimplified, one hull per call) or the
    single header-hull box when the model ships no `.phy`, uniformly `CTF_UseSimpleAndComplex`,
    with the authored mass and the resolved `PM_` physical material;
  * apply the LOD screen sizes the stage computed, Nanite as the stage decided, and
    `generate_lightmap_u_vs = False` explicitly (Lumen-only: no static lighting is ever built);
  * attach the provenance record (`UElysiumModelProvenance.apply_json`) from the staged sidecar
    plus this lane's own `shapeCount`, publish its registry tags, stamp the recipe, save;
  * author the shipped placeholder the manifest names: `Models/_Corpus/SM_Missing` (a unit
    cube wearing `MI_V2_Missing`) for a dangling `vtmb:model:` reference;
  * retain full `skinFamilies`/`familyCount` in provenance and report the canonical
    `Models/_Corpus/DA_PropSkins` finalization requirement; its owner merges static and
    skeletal inputs, and this static-only importer never overwrites that global catalogue;
  * prune inside the manifest's `pruneScope` -- `null` on a map-scoped run, which prunes nothing,
    because only `--all` knows the whole keep set;
  * write `import_report.json` beside the manifest and exit non-zero if any entry failed.

Failures are isolated per entry: an exception names the entry in the report and the run goes on.

Command line:
  -ImportModels=<path>      the manifest (required)
  -ImportUnitRoot=<path>    the export_v2 root the manifest's `unitGlb` paths hang off (required)
  -ImportForce=1            re-import every entry regardless of stamp (`0`/`false`/`no` = off)
"""
from __future__ import annotations

import gc
import json
import math
import os
import re
import struct
import time
import traceback

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402
from elysium_pipeline.asset_paths import baked_unit, corpus_path  # noqa: E402

#: The recipe stage label every fingerprint hashes under -- `importers/models.py`'s own lane name.
STAGE = "models"
PACKAGE_ROOT = "/ElysiumBaked/Models"
SETTINGS_VERSION = "elysium-model-import-v3"
MISSING_MODEL_ASSET_PATH = corpus_path("model", "SM", "Missing")
SKIN_CATALOGUE_ASSET_PATH = corpus_path("model", "DA", "PropSkins")
# Native build policy, independently fingerprinted so old half-UV assets cannot be reused.
STATIC_PRECISION_SETTINGS = {"use_full_precision_u_vs": True, "use_high_precision_tangent_basis": True}

#: Manifest schema this script understands.
MANIFEST_SCHEMA = "1.0.0"

#: The one class this lane authors.
ASSET_CLASS = "StaticMesh"

#: Entries between `unreal.collect_garbage()` calls. Smaller than the material lane's 128: each
#: entry here holds a decoded GLB, one UDynamicMesh per LOD and a freshly built StaticMesh, so
#: the resident set grows far faster per entry than a material instance does. That reasoning is
#: about what a chunk holds, not about what a boundary costs -- 32 dates from when the collect
#: was the Kismet no-op and a boundary was free, and it is unverified against the real sweep the
#: lane now pays here. `_collect_garbage` states how to re-derive it.
CHUNK = 32

#: Assets per `delete_loaded_assets` call while pruning.
PRUNE_CHUNK = 256

#: The unit's own extension, and the glTF extension name its primitives carry.
MODEL_EXTENSION = "ELYSIUM_vtmb_model"

#: `glTF metres Y-up right-handed -> Unreal centimetres Z-up left-handed`, the one rule for the
#: whole GLB ("Geometry" -> "Coordinate transform"). Kept as a named constant so the scale and the
#: axis map are stated once.
GLTF_TO_UNREAL_SCALE = 100.0

_collision = unreal.GeometryScript_Collision
_queries = unreal.GeometryScript_MeshQueries
_edits = unreal.GeometryScript_MeshEdits
_normals = unreal.GeometryScript_Normals
_lists = unreal.GeometryScript_List
_primitives = unreal.GeometryScript_Primitives


def log(msg):
    unreal.log("[import-models] %s" % msg)


def warn(msg):
    unreal.log_warning("[import-models] %s" % msg)


def fail(msg):
    unreal.log_error("[import-models] %s" % msg)


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line.

    A quoted token (`"-ImportModels=C:/path with spaces/manifest.json"`) is one token: the
    launcher quotes both paths, and a work root under a user's home may carry a space.
    """
    needle = "-%s=" % key
    for token in re.findall(r'"[^"]*"|\S+', unreal.SystemLibrary.get_command_line()):
        token = token.strip('"')
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def flag(value):
    """A command-line switch as a boolean: `1`/`true`/`yes`/`on` set it, anything else clears it.

    `bool("0")` is True, so a plain truthiness test would turn `-ImportForce=0` into a forced run.
    """
    return str(value).strip().strip('"').lower() in ("1", "true", "yes", "on")


def _collect_garbage():
    """Free what the finished chunk no longer holds, synchronously and now.

    This used to call `unreal.SystemLibrary.collect_garbage`, which is
    `UKismetSystemLibrary::CollectGarbage` and does nothing but raise
    `GEngine->ForceGarbageCollection(true)` -- a flag consumed in `UWorld::Tick`, which a
    `-run=pythonscript` commandlet never reaches. So the chunk cadence above released nothing.
    The module-level `unreal.collect_garbage` (PythonScriptPlugin, `PyCore.cpp`) calls
    `::CollectGarbage` on the spot, and in a commandlet it passes `RF_NoFlags` where the editor
    would pass `GARBAGE_COLLECTION_KEEPFLAGS`, so the RF_Standalone assets this lane just wrote
    and saved are collectable rather than kept for the rest of the run. Python's own cycle pass
    runs first: an `unreal` wrapper is a root for the collector while it lives, and one caught in
    a reference cycle is only dropped by `gc.collect()`.

    The `CHUNK` cadence above was never chosen against this call. The Kismet no-op is what these
    lanes were written on, so a chunk boundary used to cost nothing and the number only had to
    bound the resident set; a boundary is now a synchronous full sweep whose cost scales with
    everything the run has loaded, not with the chunk, and no timing has been taken. That is why
    the elapsed seconds are logged: read the per-boundary cost off the next full-corpus pass of
    this lane and set `CHUNK` from it. A partial run cannot settle it -- it sweeps a smaller
    object graph and understates the boundary.
    """
    started = time.perf_counter()
    gc.collect()
    unreal.collect_garbage()
    log("collect_garbage %.2fs" % (time.perf_counter() - started))


# --- manifest ------------------------------------------------------------------------------------


class ManifestError(RuntimeError):
    """The manifest cannot be executed as written."""


def load_manifest(path):
    """Read and shape-check the manifest; raise ManifestError rather than import from a bad one."""
    with open(path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    if not isinstance(manifest, dict):
        raise ManifestError("manifest is not an object")
    if manifest.get("schemaVersion") != MANIFEST_SCHEMA:
        raise ManifestError("manifest schema %r is not %s"
                            % (manifest.get("schemaVersion"), MANIFEST_SCHEMA))
    root = manifest.get("packageRoot")
    if root != PACKAGE_ROOT or manifest.get("producer") != STAGE:
        raise ManifestError("model manifest must name producer models and canonical root %s" % PACKAGE_ROOT)
    if manifest.get("settingsVersion") != SETTINGS_VERSION:
        raise ManifestError("model stage addressing/settings are stale; restage models with --all")
    if manifest.get("missingModelAsset") != MISSING_MODEL_ASSET_PATH:
        raise ManifestError("model placeholder must use %s" % MISSING_MODEL_ASSET_PATH)
    if manifest.get("skinCatalogueAsset") != SKIN_CATALOGUE_ASSET_PATH:
        raise ManifestError("model skin catalogue must use %s" % SKIN_CATALOGUE_ASSET_PATH)
    entries = manifest.get("assets")
    if not isinstance(entries, list):
        raise ManifestError("manifest assets is not a list")
    selection = manifest.get("selection")
    if (not isinstance(selection, dict) or "perMap" not in selection
            or not isinstance(selection.get("keys"), list)
            or not all(isinstance(key, str) for key in selection["keys"])):
        raise ManifestError("manifest lacks an explicit model selection")
    if "explicitUnits" in selection:
        explicit = selection["explicitUnits"]
        if (not isinstance(explicit, list) or not all(isinstance(id, str) for id in explicit)
                or set(explicit) != {"vtmb:model:" + key for key in selection["keys"]}
                or selection["perMap"] != {} or manifest.get("pruneScope") is not None):
            raise ManifestError("explicit unit selection must match its keys and cannot prune")

    # `pruneScope` is deliberately nullable here, unlike the material lane's: a map-scoped run
    # sets it to null and prunes nothing, "because only `--all` knows the whole keep set".
    scope = manifest.get("pruneScope")
    if scope is not None:
        if scope != PACKAGE_ROOT + "/" or selection["perMap"] is not None:
            raise ManifestError("only a whole-corpus model selection may prune the canonical Models root")
    keep = manifest.get("keep", [])
    if not isinstance(keep, list) or not all(isinstance(p, str) and p.startswith(root + "/")
                                             and all(re.fullmatch(r"[A-Za-z0-9_]+", part) for part in p[len(root)+1:].split("/"))
                                             for p in keep):
        raise ManifestError("manifest keep is not a list of asset paths below %s" % root)
    manifest["keep"] = keep

    seen, units = set(), set()
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict) or not isinstance(entry.get("recipe"), dict):
            raise ManifestError("assets[%d] is not an asset row with a recipe" % index)
        for key in ("assetPath", "unit", "unitGlb", "unitSha256", "stem", "slots", "skinFamilies",
                    "lods", "collision", "physMaterial", "nanite", "recipe"):
            if key not in entry:
                raise ManifestError("assets[%d] lacks %r" % (index, key))
        asset_path = entry["assetPath"]
        id = entry["unit"]
        try:
            expected = baked_unit(id, "SM") if isinstance(id, str) and id.startswith("vtmb:model:") else None
        except ValueError as exc:
            raise ManifestError("assets[%d] has invalid model identity: %s" % (index, exc)) from exc
        if expected is None or asset_path != expected:
            raise ManifestError("assets[%d] address %r does not match model identity %r" % (index, asset_path, id))
        if entry["unitGlb"] != "models/" + id[len("vtmb:model:"):] + ".glb":
            raise ManifestError("assets[%d] unit path does not match model identity" % index)
        if entry["recipe"].get("settingsVersion") != SETTINGS_VERSION:
            raise ManifestError("assets[%d] recipe uses stale model settings" % index)
        if asset_path.casefold() in seen or id.casefold() in units:
            raise ManifestError("assets[%d] %s is listed twice" % (index, asset_path))
        seen.add(asset_path.casefold())
        units.add(id.casefold())
    return manifest


def split_asset_path(asset_path):
    """`/Root/dir/SM_name` -> (`/Root/dir`, `SM_name`)."""
    package, _, name = asset_path.rpartition("/")
    return package, name


def sidecar_path(staging_root, entry):
    """The provenance sidecar the stage wrote beside the unit: `<staging root>/<key>.provenance.json`.

    The key is the entry's `unitGlb` without the family directory and the `.glb`, which is exactly
    what `importers.models._sidecar_path` composes -- the manifest carries no sidecar field of its
    own (unlike the material lane's `provenance`), so the one path arithmetic in this script is
    this, stated once.
    """
    relative = entry["unitGlb"]
    if relative.endswith(".glb"):
        relative = relative[:-len(".glb")]
    parts = relative.split("/")
    if parts and parts[0] == "models":
        parts = parts[1:]
    return os.path.join(staging_root, *parts[:-1], parts[-1] + ".provenance.json")


def unit_glb_path(unit_root, entry):
    return os.path.join(unit_root, *entry["unitGlb"].split("/"))


# --- glTF accessors --------------------------------------------------------------------------


#: glTF `componentType` -> (`struct` code, byte size).
_COMPONENT = {
    5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2),
    5125: ("I", 4), 5126: ("f", 4),
}
#: glTF accessor `type` -> component count.
_TYPE_COUNT = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}


def read_glb(path):
    """The GLB's JSON document and BIN chunk. Reuses the offline half's own container reader so
    the two never disagree about what a unit is."""
    from elysium_pipeline.formats.unit_contract.container import decode_glb

    with open(path, "rb") as handle:
        data = handle.read()
    return decode_glb(data, path)


def accessor(document, binary, index):
    """One accessor as a list of tuples (or of scalars for `SCALAR`), read verbatim.

    Sparse accessors are refused rather than silently read as their base: no model unit publishes
    one (the exporter never writes one for geometry), and a silent misread here is a wrong mesh.
    """
    accessors = document.get("accessors") or ()
    if not isinstance(index, int) or not (0 <= index < len(accessors)):
        raise RuntimeError("accessor %r is out of range" % (index,))
    row = accessors[index]
    if "sparse" in row:
        raise RuntimeError("accessor %d is sparse; this lane reads dense accessors only" % index)
    code, size = _COMPONENT[row["componentType"]]
    components = _TYPE_COUNT[row["type"]]
    count = int(row.get("count") or 0)
    view = (document.get("bufferViews") or ())[row["bufferView"]]
    start = int(view.get("byteOffset") or 0) + int(row.get("byteOffset") or 0)
    stride = int(view.get("byteStride") or 0) or components * size
    packed = struct.Struct("<%d%s" % (components, code))

    if stride == components * size:
        # Tightly packed: one iter_unpack pass over the whole block, which is where the corpus's
        # several million vertices actually get read.
        block = memoryview(binary)[start:start + count * stride]
        rows = list(packed.iter_unpack(block))
    else:
        rows = [packed.unpack_from(binary, start + i * stride) for i in range(count)]
    if components == 1:
        return [value[0] for value in rows]
    return rows


# --- the coordinate rule ---------------------------------------------------------------------


def unreal_positions(values):
    """`(X, Y, Z)_ue = (x, z, y)_gltf * 100`."""
    s = GLTF_TO_UNREAL_SCALE
    return [(x * s, z * s, y * s) for x, y, z in values]


def unreal_directions(values):
    """`(X, Y, Z)_ue = (x, z, y)_gltf`, re-normalized (a degenerate row stays as it came)."""
    out = []
    for x, y, z in values:
        vx, vy, vz = x, z, y
        length = math.sqrt(vx * vx + vy * vy + vz * vz)
        if length > 1e-8:
            out.append((vx / length, vy / length, vz / length))
        else:
            out.append((vx, vy, vz))
    return out


def unreal_tangents(values, normals):
    """`(TangentX, TangentY)` in Unreal space from glTF `TANGENT` (xyzw) and the already-converted
    Unreal normals.

    The axis map is the direction rule; `w_ue = -w_gltf` because the map is a reflection
    (determinant -1) and `B = w * (N x T)` -- a reflection negates the cross product, so forgetting
    the flip mirrors every normal map. `pipeline/tests/test_model_import_transform.py` asserts both
    this and the winding reversal against a hand-computed triangle.
    """
    tangents_x = []
    tangents_y = []
    for (x, y, z, w), (nx, ny, nz) in zip(values, normals):
        tx, ty, tz = x, z, y
        length = math.sqrt(tx * tx + ty * ty + tz * tz)
        if length > 1e-8:
            tx, ty, tz = tx / length, ty / length, tz / length
        sign = -float(w)
        bx = sign * (ny * tz - nz * ty)
        by = sign * (nz * tx - nx * tz)
        bz = sign * (nx * ty - ny * tx)
        tangents_x.append((tx, ty, tz))
        tangents_y.append((bx, by, bz))
    return tangents_x, tangents_y


def reversed_winding(indices):
    """The triangle list with its winding reversed **once**, at section build.

    The coordinate map is a reflection, so a triangle carried across verbatim would face inward.
    Reversing twice (here and again at export) is invisible in a screenshot of a two-sided
    surface, which is why this happens in exactly one place.
    """
    return [(indices[i], indices[i + 2], indices[i + 1]) for i in range(0, len(indices) - 2, 3)]


# --- geometry ---------------------------------------------------------------------------------


def primitive_ext(primitive):
    return ((primitive.get("extensions") or {}).get(MODEL_EXTENSION)) or {}


def submodel0_primitives(document, mesh_index):
    """Submodel 0 of each body part -- what a `sprp` placement draws, since it carries no
    bodygroup selector. The stage already recorded `multiSubmodelBakedZero` for every submodel
    dropped here."""
    meshes = document.get("meshes") or ()
    if not isinstance(mesh_index, int) or not (0 <= mesh_index < len(meshes)):
        raise RuntimeError("LOD names glTF mesh %r, which the unit does not publish" % (mesh_index,))
    return [primitive for primitive in (meshes[mesh_index].get("primitives") or ())
            if int(primitive_ext(primitive).get("model") or 0) == 0]


def decode_lod_sections(document, binary, primitives, slot_of_reference, label):
    """One decoded section per primitive, already in Unreal space.

    Read once and appended possibly twice (see `append_sections`), so the accessor decode -- the
    expensive half -- never runs twice for the same LOD.
    """
    sections = []
    for primitive in primitives:
        attributes = primitive.get("attributes") or {}
        reference = primitive_ext(primitive).get("skinReference")
        slot = slot_of_reference.get(reference)
        if slot is None:
            raise RuntimeError("%s names skinReference %r, absent from the slot list"
                               % (label, reference))

        source = primitive_ext(primitive)
        indices = accessor(document, binary, primitive["indices"])
        if "sourcePositions" in source or "sourceNormals" in source:
            from elysium_pipeline.formats.unit_contract.precision import decode
            from elysium_pipeline.formats.bsp import source_to_unreal
            from elysium_pipeline.formats.mesh_geometry import unreal_surface_normals
            count = document["accessors"][attributes["POSITION"]]["count"]
            raw_positions = decode(document, binary, source["sourcePositions"], (count, 3))
            raw_normals = decode(document, binary, source["sourceNormals"], (count, 3))
            source_positions = [tuple(raw_positions[i:i + 3]) for i in range(0, len(raw_positions), 3)]
            source_normals = [tuple(raw_normals[i:i + 3]) for i in range(0, len(raw_normals), 3)]
            positions = [source_to_unreal(*p) for p in source_positions]
            normals = unreal_surface_normals({"pos": source_positions, "nrm": source_normals,
                                              "tris": [indices[i:i + 3] for i in range(0, len(indices), 3)]})
            normals = [tuple(c / length for c in n) if (length := math.sqrt(sum(c * c for c in n))) > 1e-8
                       else n for n in normals]
        else:
            # Generic glTF geometry helpers remain usable for probes; published 2.2 model units
            # are required to carry source values by the independent unit validator.
            positions = unreal_positions(accessor(document, binary, attributes["POSITION"]))
            normals = unreal_directions(accessor(document, binary, attributes["NORMAL"]))
        uvs = accessor(document, binary, attributes["TEXCOORD_0"]) if "TEXCOORD_0" in attributes \
            else [(0.0, 0.0)] * len(positions)
        triangles = reversed_winding(indices)
        tangents = None
        if "TANGENT" in attributes:
            tangents = unreal_tangents(accessor(document, binary, attributes["TANGENT"]), normals)
        sections.append({"slot": slot, "positions": positions, "normals": normals, "uvs": uvs,
                         "triangles": triangles, "tangents": tangents})
    return sections


def needs_split(triangles):
    """Would `FDynamicMesh3::AppendTriangle` refuse any of these triangles?

    Its three refusal conditions, exactly (`DynamicMesh3_Edits.cpp`): a repeated vertex index, an
    undirected edge that already carries two triangles, and a triangle whose three vertices are
    already a triangle. Each refusal is a *silently dropped face* -- the engine logs it and carries
    on -- so this predicate runs first and the offending section is split before it is appended
    rather than after: `MakeScriptError` logs unconditionally, even with a debug object, and a
    commandlet that logs an engine error exits non-zero however well the import itself went.
    """
    edges = {}
    seen = set()
    for a, b, c in triangles:
        if a == b or b == c or a == c:
            return True
        key = (a, b, c) if a < b else (b, c, a) if b < c else (c, a, b)
        key = tuple(sorted(key))
        if key in seen:
            return True
        seen.add(key)
        for u, v in ((a, b), (b, c), (c, a)):
            edge = (u, v) if u < v else (v, u)
            count = edges.get(edge, 0) + 1
            if count > 2:
                return True
            edges[edge] = count
    return False


def _split_section(section):
    """One section re-expressed as a triangle soup: every triangle gets its own three vertices.

    Removing every shared edge makes the section trivially manifold, so no triangle can be
    refused; every per-vertex attribute is carried across unchanged, and the static-mesh build
    re-welds the identical corners into render vertices anyway. VtMB model geometry is authored
    soup and does carry non-manifold edges (16 of `wolf_form`'s 4,335 LOD-0 triangles, for one).
    """
    positions, normals, uvs = section["positions"], section["normals"], section["uvs"]
    tangents = section["tangents"]
    new_positions, new_normals, new_uvs, new_triangles = [], [], [], []
    new_tangents_x, new_tangents_y = [], []
    for a, b, c in section["triangles"]:
        base = len(new_positions)
        for index in (a, b, c):
            new_positions.append(positions[index])
            new_normals.append(normals[index])
            new_uvs.append(uvs[index])
            if tangents is not None:
                new_tangents_x.append(tangents[0][index])
                new_tangents_y.append(tangents[1][index])
        new_triangles.append((base, base + 1, base + 2))
    return {"slot": section["slot"], "positions": new_positions, "normals": new_normals,
            "uvs": new_uvs, "triangles": new_triangles,
            "tangents": (new_tangents_x, new_tangents_y) if tangents is not None else None}


def append_sections(sections, label):
    """One `UDynamicMesh` from decoded sections: one mesh section per primitive, each under its
    slot's index as the section material id, so a section maps to the same slot on every LOD.

    Returns `(mesh, has_tangents)`. Tangents are authored only when *every* primitive of the LOD
    stores them; a mixed LOD falls back to the build's own tangent recompute rather than leaving
    part of the mesh with invented tangents.
    """
    mesh = unreal.DynamicMesh()
    has_tangents = all(section["tangents"] is not None for section in sections)
    tangents_x = []
    tangents_y = []

    for section in sections:
        buffers = unreal.GeometryScriptSimpleMeshBuffers()
        buffers.vertices = [unreal.Vector(p[0], p[1], p[2]) for p in section["positions"]]
        buffers.normals = [unreal.Vector(n[0], n[1], n[2]) for n in section["normals"]]
        buffers.uv0 = [unreal.Vector2D(t[0], t[1]) for t in section["uvs"]]
        buffers.triangles = [unreal.IntVector(a, b, c) for a, b, c in section["triangles"]]
        result = _edits.append_buffers_to_mesh(
            mesh, buffers, material_id=section["slot"], defer_change_notifications=True)
        mesh = result[0] if isinstance(result, tuple) else result
        if has_tangents:
            tangents_x.extend(section["tangents"][0])
            tangents_y.extend(section["tangents"][1])

    if has_tangents:
        # The overlay is indexed by vertex id; append_buffers_to_mesh appends vertices in order,
        # so the accumulated lists line up 1:1 -- asserted rather than assumed, because a silent
        # off-by-one would rotate every tangent frame.
        vertex_count = _queries.get_num_vertex_i_ds(mesh)
        if vertex_count != len(tangents_x):
            raise RuntimeError("%s: %d vertices but %d source tangents"
                               % (label, vertex_count, len(tangents_x)))
        mesh = _normals.set_mesh_per_vertex_tangents(
            mesh,
            _lists.convert_array_to_vector_list([unreal.Vector(*t) for t in tangents_x]),
            _lists.convert_array_to_vector_list([unreal.Vector(*t) for t in tangents_y]))
    return mesh, has_tangents


def build_lod_mesh(document, binary, primitives, slot_of_reference, label):
    """One LOD's `UDynamicMesh`, with **every** source triangle in it.

    A section the dynamic mesh would refuse faces of is split before it is appended, and the
    triangle count is verified against the source afterwards either way: a short count is the
    unit's failure, named -- never an asset quietly missing faces.
    """
    sections = decode_lod_sections(document, binary, primitives, slot_of_reference, label)
    expected = sum(len(section["triangles"]) for section in sections)
    split = 0
    for index, section in enumerate(sections):
        if needs_split(section["triangles"]):
            sections[index] = _split_section(section)
            split += 1
    if split:
        log("%s: %d of %d section(s) split for non-manifold topology"
            % (label, split, len(sections)))
    mesh, has_tangents = append_sections(sections, label)
    built = _queries.get_num_triangle_i_ds(mesh)
    if built != expected:
        raise RuntimeError("%s: built %d of %d triangles" % (label, built, expected))
    return mesh, has_tangents


# --- authoring ---------------------------------------------------------------------------------


def _no_lightmap_uvs():
    """`EGeometryScriptGenerateLightmapUVOptions::DoNotGenerateLightmapUVs`.

    Resolved by name with a fallback rather than spelled once: Unreal's Python name mangling for
    an enumerator ending in an acronym (`...LightmapUVs`) is not something to guess wrong
    silently, and getting it wrong would fall back to `MatchTargetLODSetting` -- the importer
    default this lane exists to refuse.
    """
    enum = unreal.GeometryScriptGenerateLightmapUVOptions
    for name in ("DO_NOT_GENERATE_LIGHTMAP_U_VS", "DO_NOT_GENERATE_LIGHTMAP_UVS"):
        value = getattr(enum, name, None)
        if value is not None:
            return value
    raise RuntimeError("no DoNotGenerateLightmapUVs enumerator on "
                       "GeometryScriptGenerateLightmapUVOptions")


def _nanite_settings(enabled):
    settings = unreal.MeshNaniteSettings()
    settings.set_editor_property("enabled", bool(enabled))
    return settings


def _copy_options(materials, slot_names, nanite, recompute_tangents, apply_nanite):
    options = unreal.GeometryScriptCopyMeshToAssetOptions()
    options.enable_recompute_normals = False
    options.enable_recompute_tangents = bool(recompute_tangents)
    # VtMB geometry is non-manifold soup at the seams; keeping the source vertex order stops the
    # build from welding split vertices back together and smoothing the authored shading away.
    options.use_original_vertex_order = True
    options.replace_materials = True
    options.new_materials = list(materials)
    options.new_material_slot_names = list(slot_names)
    # Lumen-only: static lighting is never built, so a lightmap UV set would be dead data in
    # every one of these assets ("No `TEXCOORD_1`, and no generated lightmap UVs").
    options.generate_lightmap_u_vs = _no_lightmap_uvs()
    options.apply_nanite_settings = bool(apply_nanite)
    options.new_nanite_settings = _nanite_settings(nanite)
    options.emit_transaction = False
    return options


def author_static_mesh(asset_path, lod_meshes, materials, slot_names, nanite, has_tangents):
    """Write the LOD chain out as a `UStaticMesh` and bind its slots. Returns the asset.

    An existing mesh with the same LOD count is rewritten IN PLACE (`CopyMeshToStaticMesh` per
    LOD): deleting first routes through `ForceDeleteObjects`, whose whole-heap reference sweep
    plus GC costs seconds per asset with the corpus resident. A LOD-count change is the one case
    that recreates, because a shortened chain would otherwise keep its stale tail.
    """
    require_owned_destination(asset_path)
    package, name = split_asset_path(asset_path)
    bl.ensure_dir(package)

    existing = None
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        loaded = unreal.EditorAssetLibrary.load_asset(asset_path)
        if isinstance(loaded, unreal.StaticMesh) and loaded.get_num_lods() == len(lod_meshes):
            existing = loaded
        else:
            bl.delete_owned_asset(asset_path)

    if existing is None:
        static_precision_library()
        options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        options.enable_recompute_normals = False
        options.enable_recompute_tangents = not has_tangents
        options.enable_nanite = bool(nanite)
        options.nanite_settings = _nanite_settings(nanite)
        # A body setup has to exist before either collision helper can touch it.
        options.enable_collision = True
        options.use_original_vertex_order = True
        result = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh_lods(
            list(lod_meshes), asset_path, options)
        static_mesh = result[0] if isinstance(result, tuple) else result
        if not static_mesh:
            raise RuntimeError("create_new_static_mesh_asset_from_mesh_lods produced no asset")
        static_mesh.set_editor_property("static_materials", [
            unreal.StaticMaterial(material_interface=material, material_slot_name=slot)
            for material, slot in zip(materials, slot_names)])
        apply_static_precision(static_mesh, len(lod_meshes))
        return static_mesh

    # GeometryScript preserves these BuildSettings when copying a LOD. Set them before
    # copying new source UVs so the replacement build does not repack them into float16.
    apply_static_precision(existing, len(lod_meshes))
    for index, mesh in enumerate(lod_meshes):
        options = _copy_options(materials, slot_names, nanite, not has_tangents, index == 0)
        write_lod = unreal.GeometryScriptMeshWriteLOD()
        write_lod.set_editor_property("lod_index", index)
        result = unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(
            mesh, existing, options, write_lod)
        outcome = result[1] if isinstance(result, tuple) and len(result) > 1 else None
        if outcome is not None and outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
            raise RuntimeError("copy_mesh_to_static_mesh failed for LOD %d" % index)
    if not static_precision_matches(existing, len(lod_meshes)):
        raise RuntimeError("static precision settings changed while copying LODs: %s" % asset_path)
    return existing


_static_mesh_editor = None


def static_mesh_editor():
    """`UStaticMeshEditorSubsystem`, in a process that may have no subsystem collection.

    `GEditor->GetEditorSubsystem<>()` answers null inside `-run=pythonscript` (the editor
    subsystems may not be collected in a commandlet), so a plain `get_editor_subsystem` can fail
    a multi-LOD entry. A transient instance supports the screen-size method used here.
    Precision uses the separate commandlet-safe native library below.
    """
    global _static_mesh_editor
    if _static_mesh_editor is None:
        _static_mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        if _static_mesh_editor is None:
            _static_mesh_editor = unreal.new_object(unreal.StaticMeshEditorSubsystem)
    return _static_mesh_editor


def static_precision_matches(static_mesh, lod_count):
    if static_mesh is None or lod_count <= 0:
        return False
    return not static_precision_library().verify_precision(static_mesh, lod_count)


def static_precision_library():
    library = getattr(unreal, "ElysiumStaticMeshPrecisionLibrary", None)
    if library is None or not callable(getattr(library, "apply_precision", None)) or not callable(getattr(library, "verify_precision", None)):
        raise RuntimeError("ElysiumStaticMeshPrecisionLibrary is unavailable; rebuild the editor module before importing models")
    return library


def apply_static_precision(static_mesh, lod_count):
    """Set and verify all source/render LODs through the commandlet-safe native helper.

    The helper finishes compilation, changes only the two precision flags, performs a
    synchronous rebuild from the original source data and verifies the built buffers.
    It does not need AssetEditorSubsystem, call window APIs or clamp source UVs.
    """
    if static_mesh is None or lod_count <= 0:
        raise RuntimeError("cannot set static precision without a mesh and source LODs")
    error = static_precision_library().apply_precision(static_mesh, lod_count)
    if error:
        raise RuntimeError("static precision rebuild failed: " + error)


def apply_screen_sizes(static_mesh, screen_sizes):
    """The stage's `switchPoints -> ScreenSize` mapping, applied to the built chain.

    `SetLodScreenSizes` also clears `bAutoComputeLODScreenSize`, which is the whole point: an
    auto-computed chain would ignore VtMB's authored switch points entirely.

    A one-LOD mesh is left alone: `ScreenSize[0]` is `1.0` by the rule and is stated in provenance,
    but with no second LOD the renderer never compares it, so writing it would only disable
    auto-compute on 3,048 of the corpus's assets to no visible end. The 609 units that carry a
    real LOD chain are the ones this exists for.
    """
    if len(screen_sizes) < 2:
        return
    if not static_mesh_editor().set_lod_screen_sizes(static_mesh, [float(s) for s in screen_sizes]):
        raise RuntimeError("set_lod_screen_sizes refused %r" % (screen_sizes,))


def phy_hulls(document, binary, extension):
    """Every PHY ledge as `(vertices, triangles)` in Unreal space.

    The published hull points are `IVP metres, axis-only` -- `(x, -y, -z)` of the raw IVP triple --
    and `formats.phy`'s empirically settled IVP->Unreal map is `(x, -z, -y) * 100`; composed, that
    is exactly the same `(x, z, y) * 100` the mesh takes, which is why "one rule for the whole GLB"
    holds for the physics domain too. Triangle order is carried through unchanged: the collision
    builder takes the convex hull of the point set and never reads winding.
    """
    hulls = []
    for solid in (extension.get("physics") or {}).get("solids") or ():
        for hull in solid.get("hulls") or ():
            vertices = unreal_positions(accessor(document, binary, hull["positions"]))
            flat = accessor(document, binary, hull["indices"])
            triangles = [(flat[i], flat[i + 1], flat[i + 2]) for i in range(0, len(flat) - 2, 3)]
            hulls.append((vertices, triangles))
    return hulls


def set_bbox_collision(static_mesh, bounds):
    """The bbox rule: one box shape from `mdl.header.hullMin`/`hullMax` through `source_to_unreal`.

    It exists so the two placement modes that do not want the render trimesh have a shape --
    `solid` 2 (SOLID_BBOX) and `solid` 6 on a model that ships no `.phy`. Carrying the box is not
    a decision to use it; that ruling is the placement lane's.
    """
    low = bounds["min"]
    high = bounds["max"]
    centre = unreal.Vector((low[0] + high[0]) * 0.5, (low[1] + high[1]) * 0.5,
                           (low[2] + high[2]) * 0.5)
    size = [max(float(high[i]) - float(low[i]), 0.1) for i in range(3)]
    box = unreal.DynamicMesh()
    result = _primitives.append_box_with_collision(
        box, unreal.GeometryScriptPrimitiveOptions(), unreal.Transform(centre),
        dimension_x=size[0], dimension_y=size[1], dimension_z=size[2],
        origin=unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    # (TargetMesh, SimpleCollision) -- the box element is what this is for; the mesh is scratch.
    simple = result[1] if isinstance(result, tuple) and len(result) > 1 else None
    if simple is None:
        raise RuntimeError("append_box_with_collision produced no simple collision")
    _collision.set_simple_collision_of_static_mesh(
        simple, static_mesh, unreal.GeometryScriptSetSimpleCollisionOptions(),
        unreal.GeometryScriptSetStaticMeshCollisionOptions())
    return _collision.get_simple_collision_shape_count(simple)


def apply_collision(static_mesh, entry, document, binary, extension):
    """Cook the model's collision and return the shape count. `CTF_UseSimpleAndComplex` either
    way: a Chaos rigid body can only simulate against simple shapes while the debug pick and a
    solid static placement want a per-poly face index, and one asset serves both modes."""
    collision = entry["collision"]
    if collision["mode"] == "phy":
        hulls = phy_hulls(document, binary, extension)
        if len(hulls) != int(collision["hullCount"]):
            raise RuntimeError("unit publishes %d hull(s), the manifest states %d"
                               % (len(hulls), collision["hullCount"]))
        mass = collision.get("massKg")
        shapes = bl.set_phy_collision(
            static_mesh, {"hulls": hulls, "mass": float(mass) if mass else 0.0})
        if shapes != len(hulls):
            raise RuntimeError("cooked %d shape(s) from %d ledge(s)" % (shapes, len(hulls)))
    else:
        bounds = collision.get("hullBounds")
        if not bounds:
            raise RuntimeError("bbox collision with no hullBounds")
        shapes = set_bbox_collision(static_mesh, bounds)

    body = static_mesh.get_editor_property("body_setup")
    if body is None:
        static_mesh.create_body_setup()
        body = static_mesh.get_editor_property("body_setup")
    if body is None:
        raise RuntimeError("the mesh has no body setup")
    body.set_editor_property(
        "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX)

    # The resolved surface property, bound as asset data so the runtime applies state rather than
    # recomputing it. Always written, even when it resolves to nothing, so a stale PhysMaterial
    # from an earlier recipe never survives a re-import that no longer wants one.
    phys_path = entry.get("physMaterial")
    phys_material = unreal.load_asset(phys_path) if phys_path else None
    if phys_path and phys_material is None:
        raise RuntimeError("phys material not found: %s" % phys_path)
    if body.get_editor_property("phys_material") != phys_material:
        body.set_editor_property("phys_material", phys_material)
    return shapes


# --- the run -------------------------------------------------------------------------------------


class MaterialCache(object):
    """Loaded material instances, kept for the run. A model corpus binds far fewer distinct
    materials than it has slots, and every miss is a stage-level defect the entry must name."""

    def __init__(self):
        self._cache = {}

    def load(self, path):
        if path not in self._cache:
            self._cache[path] = unreal.load_asset(path)
        return self._cache[path]


class Tracker(object):
    """Per-asset reuse: the manifest recipe against the stamp the asset carries."""

    def __init__(self, force=False, ledger=None):
        self.force = bool(force)
        self.fingerprints = {}
        self.ledger = ledger

    def fingerprint(self, entry):
        path = entry["assetPath"]
        if path not in self.fingerprints:
            self.fingerprints[path] = bl.recipe_fingerprint(STAGE, path, {
                "source": entry["recipe"], "staticBuildSettings": STATIC_PRECISION_SETTINGS})
            if self.ledger is not None:
                self.ledger.record(path, {"source": entry["recipe"], "staticBuildSettings": STATIC_PRECISION_SETTINGS})
        return self.fingerprints[path]

    def needs_import(self, entry):
        path = entry["assetPath"]
        fingerprint = self.fingerprint(entry)
        require_owned_destination(path)
        stored = bl.stored_recipe(path, producer=STAGE)
        exists = unreal.EditorAssetLibrary.does_asset_exist(path)
        if exists and bl.asset_class_name(path) != ASSET_CLASS:
            # Author only after the unit/geometry succeeds; never delete in the reuse probe.
            return True
        if self.force or not exists:
            return True
        if stored != fingerprint:
            if self.ledger is not None:
                self.ledger.explain(path, {"source": entry["recipe"], "staticBuildSettings": STATIC_PRECISION_SETTINGS}, stored)
            return True
        lod_count = sum(not row.get("dropped") for row in entry["lods"])
        return not static_precision_matches(unreal.EditorAssetLibrary.load_asset(path), lod_count)


def selection_summary(selection):
    """The manifest's `selection` as counts, not as its 414 keys again -- the manifest beside this
    report already holds the list, and a report a human opens should say what the run covered."""
    if not isinstance(selection, dict):
        return None
    per_map = selection.get("perMap")
    return {
        "perMap": ({stem: len(keys) for stem, keys in sorted(per_map.items())}
                   if isinstance(per_map, dict) else None),
        "units": len(selection.get("keys") or ()),
    }


class Report(object):
    def __init__(self, manifest_path, package_root, selection=None,
                 anomaly_counts=None, omission_counts=None, stage_failures=None, skipped=None):
        self.manifest = manifest_path
        self.package_root = package_root
        self.selection = selection_summary(selection)
        self.built = 0
        self.reused = 0
        self.pruned = 0
        self.ownership = {"foreign": 0, "unstamped": 0}
        self.catalogue_finalization = {"assetPath": SKIN_CATALOGUE_ASSET_PATH,
                                       "requires": "merged static and skeletal catalogue authoring"}
        self.prune_deferred = False
        self.failures = []
        # Carried straight through from the stage rather than re-derived here from 414 provenance
        # sidecars a second time, exactly as the material lane's report does.
        self.anomaly_counts = dict(anomaly_counts or {})
        self.omission_counts = dict(omission_counts or {})
        self.stage_failures = list(stage_failures or [])
        self.skipped = list(skipped or [])
        self.phase_seconds = {
            "readUnit": 0.0, "geometry": 0.0, "author": 0.0, "collision": 0.0,
            "screenSizes": 0.0, "provenance": 0.0, "save": 0.0,
        }
        self.started = time.time()

    def add_phase(self, name, seconds):
        self.phase_seconds[name] = self.phase_seconds.get(name, 0.0) + seconds

    def failed(self, entry, reason):
        self.failures.append({"assetPath": entry.get("assetPath", ""),
                              "unit": entry.get("unit", ""), "reason": reason})
        fail("%s: %s" % (entry.get("assetPath", "?"), reason))

    def as_dict(self):
        return {
            "schemaVersion": "1.0.0",
            "manifest": self.manifest,
            "packageRoot": self.package_root,
            "selection": self.selection,
            "imported": self.built,
            "reused": self.reused,
            "pruned": self.pruned, **self.ownership,
            "pruneDeferred": self.prune_deferred,
            "catalogueFinalization": self.catalogue_finalization,
            "failed": self.failures,
            "stageFailures": self.stage_failures,
            "skipped": self.skipped,
            "anomalyCounts": self.anomaly_counts,
            "omissionCounts": self.omission_counts,
            "phaseSeconds": {k: round(v, 3) for k, v in sorted(self.phase_seconds.items())},
            "seconds": round(time.time() - self.started, 1),
        }

    def summary(self):
        return ("%d imported, %d reused, %d pruned, %d failed"
                % (self.built, self.reused, self.pruned, len(self.failures)))


def _finish_entry(entry, unit_root, staging_root, tracker, report, materials_cache):
    """Everything one entry needs, from GLB to save; raises on a defect."""
    t0 = time.perf_counter()
    document, binary = read_glb(unit_glb_path(unit_root, entry))
    extension = (document.get("extensions") or {}).get(MODEL_EXTENSION) or {}
    t1 = time.perf_counter()
    report.add_phase("readUnit", t1 - t0)

    slots = entry["slots"]
    slot_names = [row["slotName"] for row in slots]
    materials = []
    for row in slots:
        material = materials_cache.load(row["materialAsset"])
        if material is None:
            raise RuntimeError("slot %r material not found: %s"
                               % (row["slotName"], row["materialAsset"]))
        materials.append(material)
    slot_of_reference = {row["skinReference"]: row["index"] for row in slots}

    active = [row for row in entry["lods"] if not row.get("dropped")]
    lod_meshes = []
    has_tangents = True
    for position, row in enumerate(active):
        primitives = submodel0_primitives(document, row["mesh"])
        if not primitives:
            raise RuntimeError("LOD %s publishes no submodel-0 primitive" % (row["index"],))
        mesh, lod_tangents = build_lod_mesh(
            document, binary, primitives, slot_of_reference,
            "%s LOD %s" % (entry["unit"], row["index"]))
        has_tangents = has_tangents and lod_tangents
        lod_meshes.append(mesh)
        if position == 0 and _queries.get_num_triangle_i_ds(mesh) <= 0:
            raise RuntimeError("LOD 0 built no triangle")
    t2 = time.perf_counter()
    report.add_phase("geometry", t2 - t1)

    static_mesh = author_static_mesh(
        entry["assetPath"], lod_meshes, materials, slot_names, entry["nanite"], has_tangents)
    t3 = time.perf_counter()
    report.add_phase("author", t3 - t2)

    shape_count = apply_collision(static_mesh, entry, document, binary, extension)
    t4 = time.perf_counter()
    report.add_phase("collision", t4 - t3)

    apply_screen_sizes(static_mesh, [row["screenSize"] for row in active])
    t5 = time.perf_counter()
    report.add_phase("screenSizes", t5 - t4)

    with open(sidecar_path(staging_root, entry), "r", encoding="utf-8") as handle:
        sidecar = json.load(handle)
    # The one field the editor knows and the stage cannot: the cooked simple-shape count. Added as
    # a top-level key on the object `apply_json` parses, so `FromJson` reads it exactly like every
    # other field. Everything else is passed through verbatim -- the reader reads the shape the
    # stage writes, which `pipeline/tests/test_model_provenance_keys.py` pins.
    sidecar["shapeCount"] = int(shape_count)
    record, error = unreal.ElysiumModelProvenance.apply_json(static_mesh, json.dumps(sidecar))
    if record is None:
        raise RuntimeError("provenance rejected: %s" % error)
    stamped, error = unreal.ElysiumModelProvenance.stamp_registry_tags(static_mesh)
    if not stamped:
        raise RuntimeError("registry tags: %s" % error)
    bl.stamp_recipe(static_mesh, tracker.fingerprint(entry), producer='models')
    t6 = time.perf_counter()
    report.add_phase("provenance", t6 - t5)

    if not bl.save(entry["assetPath"]):
        raise RuntimeError("save failed")
    report.add_phase("save", time.perf_counter() - t6)
    report.built += 1


def import_entries(manifest, unit_root, staging_root, tracker, report, materials_cache=None):
    entries = manifest["assets"]
    if "explicitUnits" in manifest.get("selection", {}):
        selected = set(manifest["selection"]["explicitUnits"])
        entries = [entry for entry in entries if entry["unit"] in selected]
    if materials_cache is None:
        materials_cache = MaterialCache()
    for index, entry in enumerate(entries):
        try:
            if tracker.needs_import(entry):
                _finish_entry(entry, unit_root, staging_root, tracker, report, materials_cache)
            else:
                report.reused += 1
        except Exception as exc:  # noqa: BLE001 - isolated per entry by design
            report.failed(entry, "%s" % exc)
            if not isinstance(exc, RuntimeError):
                warn(traceback.format_exc())
        if (index + 1) % CHUNK == 0:
            _collect_garbage()
            log("[%d/%d] %s" % (index + 1, len(entries), report.summary()))
    _collect_garbage()


# --- the shipped placeholder --------------------------------------------------------------------


def author_missing_model(asset_path, material_path, tracker_force=False):
    """`SM_elysium_missing_model`: a unit cube wearing `MI_V2_Missing`.

    A dangling `vtmb:model:` reference -- 16 of them corpus-wide, all retail data bugs -- routes
    here rather than failing, so a wrong reference is visible in the world instead of invisible.
    """
    material = unreal.load_asset(material_path)
    if material is None:
        raise RuntimeError("the sentinel material is not on the mount: %s" % material_path)
    recipe = {"placeholder": "missing-model", "material": material_path, "sizeCm": 100.0,
              "staticBuildSettings": STATIC_PRECISION_SETTINGS}
    if asset_path != MISSING_MODEL_ASSET_PATH:
        raise RuntimeError("noncanonical model placeholder: %s" % asset_path)
    require_owned_destination(asset_path)
    fingerprint = bl.recipe_fingerprint(STAGE, asset_path, recipe)
    stored = bl.stored_recipe(asset_path, producer="models")
    if (not tracker_force and unreal.EditorAssetLibrary.does_asset_exist(asset_path)
            and bl.asset_class_name(asset_path) == ASSET_CLASS
            and stored == fingerprint
            and static_precision_matches(unreal.EditorAssetLibrary.load_asset(asset_path), 1)):
        return False

    mesh = unreal.DynamicMesh()
    result = _primitives.append_box(
        mesh, unreal.GeometryScriptPrimitiveOptions(), unreal.Transform(),
        dimension_x=100.0, dimension_y=100.0, dimension_z=100.0,
        origin=unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    mesh = result[0] if isinstance(result, tuple) else result
    static_mesh = author_static_mesh(
        asset_path, [mesh], [material], ["missing"], False, False)
    set_bbox_collision(static_mesh, {"min": [-50.0, -50.0, -50.0], "max": [50.0, 50.0, 50.0]})
    body = static_mesh.get_editor_property("body_setup")
    if body is not None:
        body.set_editor_property(
            "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX)
    bl.stamp_recipe(static_mesh, fingerprint, producer='models')
    if not bl.save(asset_path):
        raise RuntimeError("save failed: %s" % asset_path)
    return True


# --- skins table (R1.5) ---------------------------------------------------------------------------


#: The class the skin-table asset must already carry to be reused rather than recreated.
SKIN_SET_CLASS = "ElysiumPropSkinSet"


def author_skin_set(manifest, materials_cache, force=False):
    """Regenerate `/ElysiumBaked/Meshes/DA_ElysiumPropSkins` -- the corpus skin table
    (`docs/architecture/seam_map_model.md` -> "Import" -> "Skins table") -- from this run's own
    manifest, a finalize step over every entry rather than a per-entry one.

    The diff-against-family-0 fold that turns each entry's full, undiffed `skinFamilies` into the
    table's own short rows is `importers.model_skins.build_skin_table`, pure data with no Unreal
    (or `numpy`) dependency, so it is both pytest-tested directly and importable inside Unreal's
    embedded Python, unlike `importers.models` itself; this function only turns that data into
    `unreal.ElysiumPropSkinModel`/`ElysiumSkinFamily`/`ElysiumSkinOverride` objects and saves them,
    mirroring `pipeline/unreal/bake_map.py`'s legacy `_author_skin_set` -- same struct shape, same
    asset name -- but reading the new manifest and binding V2 `MI_` paths instead of the legacy
    per-map `.skins` sidecars.

    A whole-manifest recipe stamp skips the rebuild when nothing this run staged actually changed
    the table (map-scoped runs regenerate the same handful of multi-family models over and over
    otherwise); `force` bypasses it exactly like every other entry's stamp.
    """
    if manifest.get("packageRoot") == PACKAGE_ROOT:
        raise RuntimeError("DA_PropSkins requires merged static/skeletal catalogue authoring; a models-only fold would drop owners")
    from elysium_pipeline.importers import model_skins

    object_path = model_skins.skin_set_asset_path()
    package_root, asset_name = split_asset_path(object_path)
    rows = model_skins.build_skin_table(manifest["assets"])

    recipe = {"rows": [
        (row["stem"], row["familyCount"],
         [(family["family"], family["overrides"]) for family in row["families"]])
        for row in rows
    ]}
    fingerprint = bl.recipe_fingerprint(STAGE + ".skins", object_path, recipe)
    stored = bl.stored_recipe(object_path, producer="models")
    if (not force and unreal.EditorAssetLibrary.does_asset_exist(object_path)
            and bl.asset_class_name(object_path) == SKIN_SET_CLASS
            and stored == fingerprint):
        return {"stems": len(rows), "rows": sum(len(row["families"]) for row in rows),
                "maxFamilyCount": max((row["familyCount"] for row in rows), default=0),
                "rebuilt": False}

    models = []
    total_rows = 0
    for row in rows:
        family_rows = []
        for family in row["families"]:
            items = []
            for slot_name, material_path in family["overrides"]:
                material = materials_cache.load(material_path)
                if material is None:
                    raise RuntimeError("skin table %s family %d: material not found: %s"
                                       % (row["stem"], family["family"], material_path))
                item = unreal.ElysiumSkinOverride()
                item.set_editor_property("slot_name", slot_name)
                item.set_editor_property("material", material)
                items.append(item)
            family_row = unreal.ElysiumSkinFamily()
            family_row.set_editor_property("overrides", items)
            # Pad every family below this one that repaints nothing (including family 0) so the
            # array index stays the VtMB skin number the row states -- the same convention
            # `bake_lib.make_skin_set` uses.
            while len(family_rows) < family["family"]:
                family_rows.append(unreal.ElysiumSkinFamily())
            family_rows.append(family_row)
        model = unreal.ElysiumPropSkinModel()
        model.set_editor_property("stem", row["stem"])
        model.set_editor_property("families", family_rows)
        model.set_editor_property("family_count", row["familyCount"])
        models.append(model)
        total_rows += len(row["families"])

    bl.ensure_dir(package_root)
    asset = unreal.load_asset(object_path)
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.ElysiumPropSkinSet)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_root, unreal.ElysiumPropSkinSet, factory)
    if asset is None:
        raise RuntimeError("could not create %s" % object_path)
    asset.set_editor_property("models", models)
    bl.stamp_recipe(asset, fingerprint, producer='models')
    if not bl.save(object_path):
        raise RuntimeError("save failed: %s" % object_path)
    return {"stems": len(rows), "rows": total_rows,
            "maxFamilyCount": max((row["familyCount"] for row in rows), default=0),
            "rebuilt": True}


# --- prune ---------------------------------------------------------------------------------------


def require_owned_destination(asset_path):
    """A shared Models directory gives this producer no right to replace another asset."""
    if (not isinstance(asset_path, str) or not asset_path.startswith(PACKAGE_ROOT + "/")
            or not all(re.fullmatch(r"[A-Za-z0-9_]+", part) for part in asset_path[len(PACKAGE_ROOT)+1:].split("/"))
            or not asset_path.rsplit("/", 1)[-1].startswith("SM_")
            or (asset_path.startswith(PACKAGE_ROOT + "/_Corpus/") and asset_path != MISSING_MODEL_ASSET_PATH)):
        raise RuntimeError("outside this producer's canonical static model products: %s" % asset_path)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        owner = bl.stored_producer(asset_path)
        if owner != STAGE:
            raise RuntimeError("cannot replace %s model destination %s (producer %r)"
                               % ("foreign" if owner else "unstamped", asset_path, owner))


def prune(package_root, keep, scope, counts=None):
    if package_root != PACKAGE_ROOT or scope not in (None, PACKAGE_ROOT + "/"):
        raise ManifestError("model prune must stay in the canonical Models root")
    return bl.prune_owned(package_root, keep, scope, STAGE, counts)


def run(manifest_path, unit_root, force=False):
    manifest = load_manifest(manifest_path)
    staging_root = os.path.dirname(os.path.abspath(manifest_path))
    package_root = manifest["packageRoot"]
    report = Report(manifest_path, package_root, manifest.get("selection"),
                    anomaly_counts=manifest.get("anomalyCounts"),
                    omission_counts=manifest.get("omissionCounts"),
                    stage_failures=manifest.get("stageFailures"),
                    skipped=manifest.get("skipped"))
    log("manifest %s: %d asset(s) -> %s%s" % (
        manifest_path, len(manifest["assets"]), package_root, " (forced)" if force else ""))

    # A fresh commandlet has not indexed either mount; the stamps every reuse decision reads live
    # on the registry, and every bound material instance must resolve.
    material_root = manifest.get("missingMaterialAsset", "").rpartition("/")[0]
    scan = [package_root, "/ElysiumBaked/Materials", "/ElysiumBaked/SurfaceProperties"]
    if material_root:
        scan.append(material_root)
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(scan, force_rescan=True)

    placeholder = manifest.get("missingModelAsset")
    sentinel = manifest.get("missingMaterialAsset")
    if placeholder and sentinel:
        try:
            if author_missing_model(placeholder, sentinel, tracker_force=force):
                log("authored %s" % placeholder)
        except Exception as exc:  # noqa: BLE001
            report.failures.append({"assetPath": placeholder, "unit": "", "reason": "%s" % exc})
            fail("%s: %s" % (placeholder, exc))

    materials_cache = MaterialCache()
    ledger = bl.RecipeLedger(os.path.join(staging_root, "recipes.json"), "import-models")
    import_entries(manifest, unit_root, staging_root, Tracker(force, ledger), report, materials_cache)
    ledger.write()

    # The global table has a different producer and needs the character/wield input closure.
    # Complete static skin families remain in this manifest and on mesh provenance for that join.
    log("skin families staged for merged catalogue finalization: %s" % SKIN_CATALOGUE_ASSET_PATH)

    try:
        protected = ({entry["assetPath"] for entry in manifest["assets"]}
                    | set(manifest["keep"]) | {SKIN_CATALOGUE_ASSET_PATH})
        report.prune_deferred = bool(report.failures or report.stage_failures)
        scope = None if report.prune_deferred else manifest.get("pruneScope")
        report.pruned = prune(package_root, protected, scope, report.ownership)
    except Exception as exc:  # noqa: BLE001
        report.failures.append({"assetPath": package_root, "unit": "",
                                "reason": "prune raised: %s" % exc})
        fail("prune raised: %s" % exc)

    report_path = os.path.join(staging_root, "import_report.json")
    with open(report_path, "w", encoding="utf-8") as handle:
        json.dump(report.as_dict(), handle, indent=1, sort_keys=True)
    log("%s -> %s" % (report.summary(), report_path))
    return report


def main():
    manifest_path = cmdline_arg("ImportModels", "")
    if not manifest_path:
        raise SystemExit("[import-models] -ImportModels=<manifest.json> is required")
    unit_root = cmdline_arg("ImportUnitRoot", "")
    if not unit_root:
        raise SystemExit("[import-models] -ImportUnitRoot=<export_v2 root> is required")
    force = flag(cmdline_arg("ImportForce", ""))
    try:
        report = run(manifest_path, unit_root, force=force)
    except (ManifestError, OSError, ValueError) as exc:
        # A manifest that cannot be read, parsed or executed is one refusal, not a traceback.
        fail("manifest refused: %s" % exc)
        raise SystemExit(1)
    if report.failures:
        fail("%d entry(ies) failed; see import_report.json" % len(report.failures))
        raise SystemExit(1)


main()
