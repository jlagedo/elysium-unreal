"""Phase 2 of `uv run elysium import materials`: land the staged material corpus as Unreal
`MaterialInstanceConstant` assets.

Runs inside a headless editor (`-run=pythonscript -script=pipeline/unreal/import_materials.py
-ImportMaterials=<manifest.json>`). The offline stage already decided everything -- asset path,
parent (a generated master, or another `MI_` for a patched map material), every texture/scalar/
vector/switch, the base-property overrides, the physical material, the surface class -- and wrote
it to the manifest this script reads (`docs/architecture/seam_map_material.md` -> "Import";
`import/design/phase4_mechanics.md` §4.5). This script only executes those decisions against the
editor:

  * order every entry so a patched instance is authored strictly after the base instance it
    parents onto (`bl.make_material_instance`/`set_material_instance_parent` need the parent
    object to already exist); a base that failed this run fails every instance patched onto it
    without touching the editor for it;
  * per entry, compare the manifest's recipe against the stamp the asset carries
    (`bake_lib.RECIPE_TAG`) and touch only what is new, changed or forced;
  * `bl.make_material_instance`, bind every texture (through a small LRU so an 11k-unit run does
    not hold every texture's platform data resident), every scalar and vector, every static switch
    (`update_material_instance=False` per switch, one `update_material_instance` per instance),
    the base-property overrides, the physical material -- the entry's `switches` states every
    switch its master exposes explicitly (`True`/`False`), `basePropertyOverrides` states
    `twoSided`/`opacityMaskClipValue` explicitly too (a value or the override cleared), and
    `physMaterial` is always applied (the loaded asset or `None`), so a stale decision from an
    earlier recipe never survives a re-import that no longer wants it (review finding 2);
  * read back the parent and every texture parameter -- a parameter name the master does not
    expose is an entry failure naming it, not a silently dropped bind;
  * compile-probe (`MaterialEditingLibrary.get_statistics`, `get_num_shader_types`, `list_shaders`)
    only the first instance of each `(parent, switch-combination, blend override, two-sided,
    opacity-clip)` tuple this run actually built, and fail it when the probe reports zero
    pixel-shader instructions, zero shader types, or no hit-proxy/depth-only/base-pass shader
    among the compiled types (review finding 3);
  * attach the provenance record (`UElysiumMaterialProvenance.apply_json`), publish its registry
    tags, stamp the recipe, save;
  * prune every asset inside the manifest's `pruneScope` folder that the manifest neither names
    (`assets`) nor protects (`keep`), so a partial run never empties the rest;
  * write `import_report.json` beside the manifest and exit non-zero if any entry failed.

Failures are isolated per entry: an exception names the entry in the report and the run goes on,
except for a child instance whose base already failed this run -- that is not a fresh defect, it
is the same defect reported twice, so it is recorded without a second editor round-trip.

Command line:
  -ImportMaterials=<path>   the manifest (required)
  -ImportForce=1            re-import every entry regardless of stamp (`0`/`false`/`no` = off)
"""
from __future__ import annotations

from collections import OrderedDict
import json
import os
import re
import sys
import time
import traceback

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402

#: The recipe stage label every fingerprint hashes under.
STAGE = "materials"

#: Manifest schema this script understands.
MANIFEST_SCHEMA = "1.0.0"

#: Fallback master root, when the manifest does not carry its own (older stage output).
DEFAULT_MASTER_ROOT = "/Game/ElysiumGenerated/Materials/V2"

#: The one class this lane authors.
ASSET_CLASS = "MaterialInstanceConstant"

#: Entries between `unreal.SystemLibrary.collect_garbage()` calls -- a chunk boundary here is
#: purely a GC cadence, not a batched editor call the way the texture lane's AssetImportTask
#: chunk is: each material instance is authored one at a time regardless.
CHUNK = 128

#: Assets per `delete_loaded_assets` call while pruning.
PRUNE_CHUNK = 256

#: Loaded textures held onto across the run, oldest evicted first, so an 11k-instance run does
#: not keep every texture's platform data resident at once.
TEXTURE_LRU_CAPACITY = 2000

_mel = unreal.MaterialEditingLibrary
_tools = unreal.AssetToolsHelpers.get_asset_tools()

#: `basePropertyOverrides.blendMode` -> `unreal.BlendMode` member name, mirroring the strings
#: `importers/materials.py` writes (`docs/architecture/seam_map_material.md` -> "Master
#: inventory").
BLEND_MODE_MEMBERS = {
    "Opaque": "BLEND_OPAQUE",
    "Masked": "BLEND_MASKED",
    "Translucent": "BLEND_TRANSLUCENT",
    "Additive": "BLEND_ADDITIVE",
    "Modulate": "BLEND_MODULATE",
}


def log(msg):
    unreal.log("[import-materials] %s" % msg)


def warn(msg):
    unreal.log_warning("[import-materials] %s" % msg)


def fail(msg):
    unreal.log_error("[import-materials] %s" % msg)


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line.

    A quoted token (`"-ImportMaterials=C:/path with spaces/manifest.json"`) is one token: the
    launcher quotes the manifest path, and a work root under a user's home may carry a space.
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
    collect = getattr(unreal.SystemLibrary, "collect_garbage", None)
    if collect:
        collect()


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
        raise ManifestError("manifest schema %r is not %s" % (manifest.get("schemaVersion"), MANIFEST_SCHEMA))
    root = manifest.get("packageRoot")
    if not isinstance(root, str) or not root.startswith("/") or root.endswith("/"):
        raise ManifestError("manifest packageRoot %r is not a mount path" % (root,))
    master_root = manifest.get("masterRoot", DEFAULT_MASTER_ROOT)
    if not isinstance(master_root, str) or not master_root.startswith("/") or master_root.endswith("/"):
        raise ManifestError("manifest masterRoot %r is not a mount path" % (master_root,))
    manifest["masterRoot"] = master_root
    entries = manifest.get("assets")
    if not isinstance(entries, list):
        raise ManifestError("manifest assets is not a list")
    select = manifest.get("select")
    if select is not None and (not isinstance(select, str) or not select.strip("/")):
        raise ManifestError("manifest select %r is neither null nor a key directory" % (select,))
    scope = manifest.get("pruneScope", root + "/")
    if (not isinstance(scope, str) or not scope.endswith("/")
            or not scope.lower().startswith((root + "/").lower())):
        raise ManifestError("manifest pruneScope %r is not a folder below %s" % (scope, root))
    manifest["pruneScope"] = scope
    keep = manifest.get("keep", [])
    if not isinstance(keep, list) or not all(isinstance(path, str) and path.startswith(root + "/")
                                             for path in keep):
        raise ManifestError("manifest keep is not a list of asset paths below %s" % root)
    manifest["keep"] = keep

    allowed_parent_roots = (master_root + "/", root + "/")
    seen = set()
    for index, entry in enumerate(entries):
        for key in ("assetPath", "unit", "unitSha256", "parent", "textures", "scalars",
                    "vectors", "switches", "basePropertyOverrides", "provenance", "recipe"):
            if key not in entry:
                raise ManifestError("assets[%d] lacks %r" % (index, key))
        asset_path = entry["assetPath"]
        if not isinstance(asset_path, str) or not asset_path.startswith(root + "/"):
            raise ManifestError("assets[%d] %s is outside %s" % (index, asset_path, root))
        parent = entry["parent"]
        if not isinstance(parent, str) or not parent.startswith(allowed_parent_roots):
            raise ManifestError(
                "assets[%d] %s has parent %r outside %s or %s"
                % (index, asset_path, parent, master_root, root))
        if asset_path in seen:
            raise ManifestError("assets[%d] %s is listed twice" % (index, asset_path))
        seen.add(asset_path)
    return manifest


def split_asset_path(asset_path):
    """`/Root/dir/MI_name` -> (`/Root/dir`, `MI_name`)."""
    package, _, name = asset_path.rpartition("/")
    return package, name


def topo_order(entries):
    """Entries ordered so a patched instance's base -- when the base is itself in this manifest
    -- is processed first. Kahn's algorithm over the `parent` edges that stay inside the
    manifest; a parent outside it (an ordinary unit's master, or a patched unit whose base failed
    to stage and so is simply absent) has no edge and its dependents are ready immediately.

    Raises ManifestError on a cycle, which the shape of `patch.asset` resolution should never
    produce, but a defect upstream should be caught here rather than hang the run.
    """
    by_path = {entry["assetPath"]: entry for entry in entries}
    indegree = {path: 0 for path in by_path}
    children = {path: [] for path in by_path}
    for entry in entries:
        parent = entry["parent"]
        if parent in by_path:
            children[parent].append(entry["assetPath"])
            indegree[entry["assetPath"]] += 1

    queue = [entry["assetPath"] for entry in entries if indegree[entry["assetPath"]] == 0]
    order = []
    queued = set(queue)
    head = 0
    while head < len(queue):
        path = queue[head]
        head += 1
        order.append(by_path[path])
        for child in children[path]:
            indegree[child] -= 1
            if indegree[child] == 0 and child not in queued:
                queue.append(child)
                queued.add(child)
    if len(order) != len(entries):
        raise ManifestError("assets[] parent references form a cycle")
    return order


# --- textures --------------------------------------------------------------------------------


class TextureCache(object):
    """An LRU of loaded textures, oldest evicted first, bounded to `capacity`.

    Holding a plain reference is enough to keep an already-loaded texture's platform data
    resident across entries that reuse it (a shared `T_LinearWhiteMask` or a corpus texture two
    instances bind); the eviction bound is what stops an 11k-instance run from accumulating every
    texture the corpus ever names.
    """

    def __init__(self, capacity=TEXTURE_LRU_CAPACITY):
        self.capacity = capacity
        self._cache = OrderedDict()

    def load(self, path):
        if path in self._cache:
            self._cache.move_to_end(path)
            return self._cache[path]
        texture = unreal.load_asset(path)
        if texture is not None:
            self._cache[path] = texture
            if len(self._cache) > self.capacity:
                self._cache.popitem(last=False)
        return texture


# --- the run -------------------------------------------------------------------------------------


class Tracker(object):
    """Per-asset reuse: the manifest recipe against the stamp the asset carries.

    Force rebuilds; a missing asset builds; an asset of the wrong class is deleted and rebuilt; a
    stamp equal to the recipe fingerprint is reused.
    """

    def __init__(self, force=False):
        self.force = bool(force)
        self.fingerprints = {}

    def fingerprint(self, entry):
        path = entry["assetPath"]
        if path not in self.fingerprints:
            self.fingerprints[path] = bl.recipe_fingerprint(STAGE, path, entry["recipe"])
        return self.fingerprints[path]

    def needs_import(self, entry):
        path = entry["assetPath"]
        fingerprint = self.fingerprint(entry)
        exists = unreal.EditorAssetLibrary.does_asset_exist(path)
        if exists and bl.asset_class_name(path) != ASSET_CLASS:
            bl.delete_owned_asset(path)
            exists = False
        if self.force or not exists:
            return True
        return bl.stored_recipe(path) != fingerprint


class Report(object):
    def __init__(self, manifest_path, package_root, select=None, anomaly_counts=None, omission_counts=None):
        self.manifest = manifest_path
        self.package_root = package_root
        self.select = select
        self.built = 0
        self.reused = 0
        self.pruned = 0
        self.provenance_only = 0
        self.failures = []
        self.compiled_permutations = []
        # Review finding 5: the offline stage already tallied every anomaly/omission kind across
        # the whole corpus (`materials.py::stage_materials`); carried straight through into
        # `import_report.json` rather than re-derived here from 19,125 provenance sidecars a
        # second time.
        self.anomaly_counts = dict(anomaly_counts or {})
        self.omission_counts = dict(omission_counts or {})
        self.started = time.time()

    def failed(self, entry, reason):
        self.failures.append({"assetPath": entry["assetPath"], "unit": entry.get("unit", ""), "reason": reason})
        fail("%s: %s" % (entry["assetPath"], reason))

    def as_dict(self):
        """The `import_report.json` body. `imported`, `reused`, `pruned` and `failed` (a list of
        `{assetPath, unit, reason}`) are what the CLI reads back; the rest is for the reader."""
        return {
            "schemaVersion": "1.0.0",
            "manifest": self.manifest,
            "packageRoot": self.package_root,
            "select": self.select,
            "imported": self.built,
            "reused": self.reused,
            "pruned": self.pruned,
            "provenanceOnly": self.provenance_only,
            "failed": self.failures,
            "compiledPermutations": self.compiled_permutations,
            "anomalyCounts": self.anomaly_counts,
            "omissionCounts": self.omission_counts,
            "seconds": round(time.time() - self.started, 1),
        }

    def summary(self):
        return ("%d imported, %d reused, %d pruned, %d failed"
                % (self.built, self.reused, self.pruned, len(self.failures)))


def _blend_mode_value(name):
    member = BLEND_MODE_MEMBERS.get(name)
    if member is None:
        raise RuntimeError("basePropertyOverrides.blendMode %r is unknown" % (name,))
    return getattr(unreal.BlendMode, member)


def _apply_base_property_overrides(mic, overrides):
    """Every override flag is set explicitly, true-with-a-value or false -- never left untouched
    (review finding 2). A patched instance's `overrides == {}` (never authors a blend/two-sided/
    clip decision of its own) clears all three explicitly rather than leaving whatever a previous
    run's stamp left behind; `materials.py::_resolve_blend` always states `blendMode`/`twoSided`
    for a non-patched entry, and `opacityMaskClipValue` only for a `Masked` blend (absent, not
    `None`, on every other blend mode -- both read the same way here)."""
    bpo = mic.get_editor_property("base_property_overrides")

    blend_mode = overrides.get("blendMode")
    bpo.set_editor_property("override_blend_mode", blend_mode is not None)
    if blend_mode is not None:
        bpo.set_editor_property("blend_mode", _blend_mode_value(blend_mode))

    bpo.set_editor_property("override_two_sided", "twoSided" in overrides)
    if "twoSided" in overrides:
        bpo.set_editor_property("two_sided", bool(overrides["twoSided"]))

    clip = overrides.get("opacityMaskClipValue")
    bpo.set_editor_property("override_opacity_mask_clip_value", clip is not None)
    if clip is not None:
        bpo.set_editor_property("opacity_mask_clip_value", float(clip))

    mic.set_editor_property("base_property_overrides", bpo)


def _apply_switches(mic, switches):
    # `update_material_instance=False` on every switch: the one instance-wide refresh happens in
    # `_finish_entry`, after `_apply_base_property_overrides` too -- see the comment there.
    for name, value in switches.items():
        _mel.set_material_instance_static_switch_parameter_value(
            mic, name, bool(value), update_material_instance=False)


def _permutation_key(entry):
    # Review finding 3: the base-property overrides are part of the compiled permutation too --
    # `TwoSided` and a non-default `OpacityMaskClipValue` each select their own shader map the
    # same way a static switch does (`_finish_entry`'s own `update_material_instance` comment
    # explains why `TwoSided` alone forces a fresh hit-proxy permutation) -- `blendMode` alone
    # measured 963 keys against 1,000 real permutations; folding all three in closes that gap.
    overrides = entry["basePropertyOverrides"]
    return (entry["parent"], tuple(sorted(entry["switches"].items())),
            overrides.get("blendMode"), bool(overrides.get("twoSided")),
            overrides.get("opacityMaskClipValue"))


#: Substrings (case-insensitive) `list_shaders`' `shader_type_name` is checked against: the probe
#: must find at least one hit-proxy shader (editor selection/outlining -- `SceneHitProxyRendering.cpp`)
#: and one depth shader (`FDepthOnlyVS`/`FDepthOnlyPS` and friends) among the compiled types, not
#: only a non-zero base-pass instruction count.
_REQUIRED_SHADER_TYPE_SUBSTRINGS = ("hitproxy", "depthonly", "basepass")


def _compile_probe(mic, entry, report, probed):
    key = _permutation_key(entry)
    if key in probed:
        return
    probed.add(key)
    stats = _mel.get_statistics(mic)
    instructions = getattr(stats, "num_pixel_shader_instructions", None)
    if instructions is None:
        instructions = stats.get_editor_property("num_pixel_shader_instructions")

    num_shader_types = _mel.get_num_shader_types(mic)
    shader_type_names = sorted({
        str(getattr(row, "shader_type_name", "") or row.get_editor_property("shader_type_name"))
        for row in _mel.list_shaders(mic)
    })
    lowered = [name.lower() for name in shader_type_names]
    missing_required = [
        needle for needle in _REQUIRED_SHADER_TYPE_SUBSTRINGS
        if not any(needle in name for name in lowered)
    ]

    report.compiled_permutations.append({
        "assetPath": entry["assetPath"],
        "parent": entry["parent"],
        "switches": dict(entry["switches"]),
        "blendMode": entry["basePropertyOverrides"].get("blendMode"),
        "twoSided": bool(entry["basePropertyOverrides"].get("twoSided")),
        "opacityMaskClipValue": entry["basePropertyOverrides"].get("opacityMaskClipValue"),
        "numPixelShaderInstructions": instructions,
        "numShaderTypes": num_shader_types,
        "probedShaderTypes": shader_type_names,
    })
    if instructions <= 0:
        raise RuntimeError(
            "compile probe reports %d pixel-shader instructions" % instructions)
    if num_shader_types <= 0:
        raise RuntimeError("compile probe reports 0 shader types")
    if missing_required:
        raise RuntimeError(
            "compile probe found no %s shader among %d compiled type(s)"
            % (" or ".join(missing_required), num_shader_types))


def _finish_entry(entry, staging_root, tracker, report, textures, probed):
    """Everything one entry needs, from parenting to save; raises on a defect."""
    parent_path = entry["parent"]
    parent = unreal.load_asset(parent_path)
    if parent is None:
        raise RuntimeError("parent not found: %s" % parent_path)

    package, name = split_asset_path(entry["assetPath"])
    mic = bl.make_material_instance(name, package, parent)
    if mic is None:
        raise RuntimeError("make_material_instance produced no asset")

    for param, texture_path in entry["textures"].items():
        texture = textures.load(texture_path)
        if texture is None:
            raise RuntimeError("texture not found: %s (parameter %s)" % (texture_path, param))
        bl.set_tex_param(mic, param, texture)

    for param, value in entry["scalars"].items():
        bl.set_scalar_param(mic, param, float(value))

    for param, value in entry["vectors"].items():
        components = list(value) + [1.0] * (4 - len(value)) if len(value) < 4 else list(value)
        bl.set_vector_param(mic, param, unreal.LinearColor(*components[:4]))

    _apply_switches(mic, entry["switches"])
    _apply_base_property_overrides(mic, entry["basePropertyOverrides"])
    # One refresh, after every static-switch and base-property-override write: `TwoSided` (like
    # any base-property override) is not part of the static parameter set the switches' own
    # update would have rebuilt against, so an instance whose *only* reason to need the editor's
    # hit-proxy shader permutation is its `TwoSided` override -- opaque, writes every pixel,
    # nothing else forcing it -- would otherwise probe a shader map built from a snapshot older
    # than the override, and UE 5.8 asserts (`ShaderMapId.ContainsShaderType`, "missing expected
    # shader type FHitProxyVS") rather than silently recompiling. Refreshing once here, after both
    # switches and overrides have landed and before the compile probe touches the resource, keeps
    # the shader map's cache key and its live `ShouldCache` evaluation looking at the same state.
    _mel.update_material_instance(mic)

    # Review finding 2: always explicit, even when the entry's own is `None` (a patched instance,
    # or a unit whose physical material fallback resolved to nothing) -- a stale `PhysMaterial`
    # from a prior recipe would otherwise survive a re-import that no longer wants one.
    phys_material_path = entry.get("physMaterial")
    phys_material = unreal.load_asset(phys_material_path) if phys_material_path else None
    if phys_material_path and phys_material is None:
        raise RuntimeError("phys material not found: %s" % phys_material_path)
    mic.set_editor_property("phys_material", phys_material)

    # Read-back: the parent is the manifest's, and every texture parameter reads back the asset
    # this entry bound -- a parameter name the master (or, for a patched unit, the base instance)
    # does not expose reads back None or a mismatch, and that is this entry's failure, named.
    read_parent = mic.get_editor_property("parent")
    if read_parent != parent:
        raise RuntimeError("parent read-back mismatch")
    for param, texture_path in entry["textures"].items():
        bound = _mel.get_material_instance_texture_parameter_value(mic, param)
        expected = textures.load(texture_path)
        if bound != expected:
            raise RuntimeError("unknown or unbound texture parameter %r" % (param,))

    _compile_probe(mic, entry, report, probed)

    with open(os.path.join(staging_root, entry["provenance"].replace("/", os.sep)), "r",
              encoding="utf-8") as handle:
        sidecar = json.loads(handle.read())

    # Four fields `UElysiumMaterialProvenance::FromJson` reads live in the manifest entry, not the
    # provenance sidecar itself: `assetPath` and `unitGlb` are staged only once, on the entry, not
    # duplicated onto every unit's sidecar; `sourceMembersSha256` (review finding 9, renamed from
    # `sourceSha256`) sits beside `unitSha256` on the entry; `physMaterial` is the entry's resolved
    # phys-material asset path (`SurfacePropertyAsset`, mirroring `PhysMaterial`). Added as
    # top-level keys on the object `apply_json` parses, not a nested `"manifest"` object, so
    # `FromJson` reads them exactly like every other top-level field.
    for key in ("assetPath", "unitGlb", "sourceMembersSha256", "physMaterial"):
        if key in entry:
            sidecar[key] = entry[key]

    record, error = unreal.ElysiumMaterialProvenance.apply_json(mic, json.dumps(sidecar))
    if record is None:
        raise RuntimeError("provenance rejected: %s" % error)
    stamped, error = unreal.ElysiumMaterialProvenance.stamp_registry_tags(mic)
    if not stamped:
        raise RuntimeError("registry tags: %s" % error)

    bl.stamp_recipe(mic, tracker.fingerprint(entry))
    if not bl.save(entry["assetPath"]):
        raise RuntimeError("save failed")

    if entry.get("provenanceOnly"):
        report.provenance_only += 1
    report.built += 1


def import_entries(manifest, staging_root, tracker, report):
    """Author every entry in dependency order; a base's failure fails its patched children
    without touching the editor for them."""
    ordered = topo_order(manifest["assets"])
    textures = TextureCache()
    probed = set()
    failed_paths = set()

    for index, entry in enumerate(ordered):
        path = entry["assetPath"]
        parent_path = entry["parent"]
        if parent_path in failed_paths:
            report.failed(entry, "base instance failed: %s" % parent_path)
            failed_paths.add(path)
            continue
        try:
            if tracker.needs_import(entry):
                _finish_entry(entry, staging_root, tracker, report, textures, probed)
            else:
                report.reused += 1
                if entry.get("provenanceOnly"):
                    report.provenance_only += 1
        except Exception as exc:  # noqa: BLE001 - isolated per entry by design
            report.failed(entry, "%s" % exc)
            failed_paths.add(path)
            if not isinstance(exc, RuntimeError):
                warn(traceback.format_exc())

        if (index + 1) % CHUNK == 0:
            _collect_garbage()
            log("[%d/%d] %s" % (index + 1, len(ordered), report.summary()))
    _collect_garbage()


def prune(package_root, keep, scope):
    """Delete every asset inside `scope` the manifest neither names nor protects, then empty folders.

    `keep` holds package paths below `package_root`: the manifest's `assets` plus its `keep`
    list. `scope` is the manifest's `pruneScope`, always with a trailing slash, compared
    case-insensitively. This lane never prunes the master root -- `pruneScope` is validated to
    sit below `packageRoot`, so the hand-authored masters under `masterRoot` are never in scope.
    """
    library = unreal.EditorAssetLibrary
    if not library.does_directory_exist(package_root):
        return 0
    scope = scope.lower()
    if not scope.endswith("/"):
        scope += "/"
    stale = []
    for object_path in library.list_assets(package_root, recursive=True, include_folder=False):
        package_path = object_path.split(".", 1)[0]
        if package_path.lower().startswith(scope) and package_path not in keep:
            stale.append(package_path)
    for start in range(0, len(stale), PRUNE_CHUNK):
        bl.delete_owned_assets(stale[start:start + PRUNE_CHUNK])
    folders = [path for path in library.list_assets(package_root, recursive=True, include_folder=True)
               if path.endswith("/") and path.lower().startswith(scope)]
    for folder in sorted(folders, key=lambda p: p.count("/"), reverse=True):
        if not library.list_assets(folder, recursive=True, include_folder=False):
            library.delete_directory(folder)
    return len(stale)


def run(manifest_path, force=False):
    manifest = load_manifest(manifest_path)
    staging_root = os.path.dirname(os.path.abspath(manifest_path))
    package_root = manifest["packageRoot"]
    master_root = manifest["masterRoot"]
    select = manifest.get("select")
    report = Report(manifest_path, package_root, select,
                    anomaly_counts=manifest.get("anomalyCounts"),
                    omission_counts=manifest.get("omissionCounts"))
    log("manifest %s: %d asset(s) -> %s%s%s" % (
        manifest_path, len(manifest["assets"]), package_root,
        " (select %s)" % select if select else "", " (forced)" if force else ""))

    # A fresh commandlet has not indexed either mount; the stamps every reuse decision reads live
    # on the registry, and both the masters and any already-imported patched base must resolve
    # for a parent load to succeed.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [package_root, master_root], force_rescan=True)

    import_entries(manifest, staging_root, Tracker(force), report)
    try:
        protected = {entry["assetPath"] for entry in manifest["assets"]} | set(manifest["keep"])
        report.pruned = prune(package_root, protected, manifest["pruneScope"])
    except Exception as exc:  # noqa: BLE001
        report.failures.append({"assetPath": package_root, "unit": "", "reason": "prune raised: %s" % exc})
        fail("prune raised: %s" % exc)

    report_path = os.path.join(staging_root, "import_report.json")
    with open(report_path, "w", encoding="utf-8") as handle:
        json.dump(report.as_dict(), handle, indent=1, sort_keys=True)
    log("%s -> %s" % (report.summary(), report_path))
    return report


def main():
    manifest_path = cmdline_arg("ImportMaterials", "")
    if not manifest_path:
        raise SystemExit("[import-materials] -ImportMaterials=<manifest.json> is required")
    force = flag(cmdline_arg("ImportForce", ""))
    try:
        report = run(manifest_path, force=force)
    except (ManifestError, OSError, ValueError) as exc:
        # A manifest that cannot be read, parsed or executed is one refusal, not a traceback.
        fail("manifest refused: %s" % exc)
        raise SystemExit(1)
    if report.failures:
        fail("%d entry(ies) failed; see import_report.json" % len(report.failures))
        raise SystemExit(1)


main()
