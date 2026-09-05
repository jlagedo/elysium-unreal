"""Phase 2 of `uv run elysium import surface-properties`: land the staged sidecars as assets.

Runs inside a headless editor (`-run=pythonscript
-script=pipeline/unreal/import_surface_properties.py -ImportSurfaceProperties=<manifest.json>`).
The offline stage already decided everything -- asset path, the flattened values, which entry in
the base chain authored each one, and the `EPhysicalSurface` row the entry's `gamematerial` maps
to -- and wrote it to the manifest and the per-unit sidecars this script reads
(`docs/architecture/seam_map_surface_property.md` -> "Import"). This script only executes those
decisions against the editor:

  * per entry, compare the manifest's recipe against the stamp the asset carries
    (`bake_lib.RECIPE_TAG`) and touch only what is new, changed or forced;
  * create (or reuse) one `UElysiumPhysicalMaterial` per entry, apply the sidecar
    (`UElysiumPhysicalMaterial.apply_json`, which also attaches the provenance record), publish
    its registry tags, stamp the recipe, save;
  * prune every asset inside the manifest's `pruneScope` folder that the manifest neither names
    (`assets`) nor protects (`keep`: the asset paths of units the stage could not resolve), so a
    transient stage failure never deletes a previously good asset;
  * write `import_report.json` beside the manifest and exit non-zero if any entry failed.

Failures are isolated per entry: an exception names the entry in the report and the run goes on.
The corpus is 63 assets, so this is a seconds-long run whose cost is editor boot.

Command line:
  -ImportSurfaceProperties=<path>   the manifest (required)
  -ImportForce=1                    re-author every entry regardless of stamp (`0`/`false`/`no` = off)
"""
from __future__ import annotations

import json
import os
import re
import time
import traceback

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402

#: The recipe stage label every fingerprint hashes under. A fingerprint also hashes the object
#: path, and this lane owns its package root outright, so no stamp can be mistaken for another's.
STAGE = "surface-properties"

#: Manifest schema this script understands.
MANIFEST_SCHEMA = "1.0.0"

#: The one class this lane authors.
ASSET_CLASS = "ElysiumPhysicalMaterial"

#: Assets per `delete_loaded_assets` call while pruning.
PRUNE_CHUNK = 256

_tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("[import-surface-properties] %s" % msg)


def warn(msg):
    unreal.log_warning("[import-surface-properties] %s" % msg)


def fail(msg):
    unreal.log_error("[import-surface-properties] %s" % msg)


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line.

    A quoted token (`"-ImportSurfaceProperties=C:/path with spaces/manifest.json"`) is one token:
    the launcher quotes the manifest path, and a work root under a user's home may carry a space.
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


# --- manifest ------------------------------------------------------------------------------------


class ManifestError(RuntimeError):
    """The manifest cannot be executed as written."""


def load_manifest(path):
    """Read and shape-check the manifest; raise ManifestError rather than author from a bad one."""
    with open(path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    if not isinstance(manifest, dict):
        raise ManifestError("manifest is not an object")
    if manifest.get("schemaVersion") != MANIFEST_SCHEMA:
        raise ManifestError("manifest schema %r is not %s"
                            % (manifest.get("schemaVersion"), MANIFEST_SCHEMA))
    root = manifest.get("packageRoot")
    if not isinstance(root, str) or not root.startswith("/") or root.endswith("/"):
        raise ManifestError("manifest packageRoot %r is not a mount path" % (root,))
    entries = manifest.get("assets")
    if not isinstance(entries, list):
        raise ManifestError("manifest assets is not a list")
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
    seen = set()
    for index, entry in enumerate(entries):
        for key in ("assetPath", "class", "provenance", "unit", "recipe", "surfaceType"):
            if key not in entry:
                raise ManifestError("assets[%d] lacks %r" % (index, key))
        if not entry["assetPath"].startswith(root + "/"):
            raise ManifestError("assets[%d] %s is outside %s" % (index, entry["assetPath"], root))
        if entry["class"] != ASSET_CLASS:
            raise ManifestError("assets[%d] class %r is not %s" % (index, entry["class"], ASSET_CLASS))
        if entry["assetPath"] in seen:
            raise ManifestError("assets[%d] %s is listed twice" % (index, entry["assetPath"]))
        seen.add(entry["assetPath"])
    return manifest


def split_asset_path(asset_path):
    """`/Root/PM_name` -> (`/Root`, `PM_name`)."""
    package, _, name = asset_path.rpartition("/")
    return package, name


# --- the run -------------------------------------------------------------------------------------


class Tracker(object):
    """Per-asset reuse: the manifest recipe against the stamp the asset carries.

    The same decision the texture lane makes: force re-authors; a missing asset is created; an
    asset of the wrong class is deleted and recreated; a stamp equal to the recipe fingerprint is
    reused. The recipe carries a digest over the whole base chain as well as the unit's own hash,
    because a flattened asset's values change when an ancestor's unit does.
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
        stored = bl.stored_recipe(path, producer='surface-properties')
        exists = unreal.EditorAssetLibrary.does_asset_exist(path)
        if exists and bl.asset_class_name(path) != entry["class"]:
            bl.delete_owned_asset(path)
            exists = False
        if self.force or not exists:
            return True
        return stored != fingerprint


class Report(object):
    def __init__(self, manifest_path, package_root):
        self.manifest = manifest_path
        self.package_root = package_root
        self.built = 0
        self.reused = 0
        self.pruned = 0
        self.ownership = {"foreign": 0, "unstamped": 0}
        self.failures = []
        self.surface_types = {}
        self.started = time.time()

    def failed(self, entry, reason):
        self.failures.append({"assetPath": entry["assetPath"], "unit": entry.get("unit", ""),
                              "reason": reason})
        fail("%s: %s" % (entry["assetPath"], reason))

    def as_dict(self):
        """The `import_report.json` body. `imported`, `reused`, `pruned` and `failed` (a list of
        `{assetPath, unit, reason}`) are what the CLI reads back; the rest is for the reader."""
        return {
            "schemaVersion": "1.0.0",
            "manifest": self.manifest,
            "packageRoot": self.package_root,
            "imported": self.built,
            "reused": self.reused,
            "pruned": self.pruned, **self.ownership,
            "failed": self.failures,
            "surfaceTypes": self.surface_types,
            "seconds": round(time.time() - self.started, 1),
        }

    def summary(self):
        return ("%d imported, %d reused, %d pruned, %d failed"
                % (self.built, self.reused, self.pruned, len(self.failures)))


def ensure_asset(entry):
    """The `UElysiumPhysicalMaterial` at the entry's path, created if it is not already there.

    Resolved by load-first, then create: `does_asset_exist` reports False for an asset already on
    this mount even after a forced rescan, and `create_asset` then trips the unattended overwrite
    guard. The load goes through `unreal.load_asset` (LoadObject) rather than the
    EditorAssetLibrary one, which logs a hard *Error* when the registry has no such asset -- on a
    first run that is the normal path, and it would make a clean run report failure.

    `UPhysicalMaterialFactoryNew` creates whatever class `create_asset` hands it (it only falls
    back to its own `PhysicalMaterialClass` when one is set), and AssetTools already checks that
    class against the factory's supported class -- so naming `ElysiumPhysicalMaterial` here is
    what makes the asset ours rather than a stock physical material.
    """
    path = entry["assetPath"]
    package, name = split_asset_path(path)
    asset = unreal.load_asset(path)
    if asset is not None and asset.get_class().get_name() != entry["class"]:
        bl.delete_owned_asset(path)
        asset = None
    if asset is None:
        bl.ensure_dir(package)
        asset = _tools.create_asset(name, package, unreal.ElysiumPhysicalMaterial,
                                    unreal.PhysicalMaterialFactoryNew())
    if asset is None:
        raise RuntimeError("create_asset produced no asset")
    built = asset.get_class().get_name()
    if built != entry["class"]:
        raise RuntimeError("created as %s, expected %s" % (built, entry["class"]))
    return asset


def _finish_entry(entry, staging_root, tracker, report):
    """Everything one entry needs, from creation to save; raises on a defect."""
    material = ensure_asset(entry)

    with open(os.path.join(staging_root, entry["provenance"].replace("/", os.sep)), "r",
              encoding="utf-8") as handle:
        sidecar = handle.read()
    applied, error = unreal.ElysiumPhysicalMaterial.apply_json(material, sidecar)
    if not applied:
        raise RuntimeError("sidecar rejected: %s" % error)
    stamped, error = unreal.ElysiumPhysicalMaterial.stamp_registry_tags(material)
    if not stamped:
        raise RuntimeError("registry tags: %s" % error)

    bl.stamp_recipe(material, tracker.fingerprint(entry), producer='surface-properties')
    if not bl.save(entry["assetPath"]):
        raise RuntimeError("save failed")

    row = entry["surfaceType"]
    report.surface_types[row] = report.surface_types.get(row, 0) + 1
    report.built += 1


def import_entries(manifest, staging_root, tracker, report):
    """Decide, author, save; every defect lands in the report and the run goes on."""
    pending = []
    for entry in manifest["assets"]:
        try:
            if tracker.needs_import(entry):
                pending.append(entry)
            else:
                report.reused += 1
        except Exception as exc:  # noqa: BLE001 - isolated per entry by design
            report.failed(entry, "reuse check raised: %s" % exc)
    log("%d to author, %d reused" % (len(pending), report.reused))

    for entry in pending:
        try:
            _finish_entry(entry, staging_root, tracker, report)
        except Exception as exc:  # noqa: BLE001
            report.failed(entry, "%s" % exc)
            if not isinstance(exc, RuntimeError):
                warn(traceback.format_exc())


def prune(package_root, keep, scope, counts=None):
    return bl.prune_owned(package_root, keep, scope, 'surface-properties', counts)


def run(manifest_path, force=False):
    manifest = load_manifest(manifest_path)
    staging_root = os.path.dirname(os.path.abspath(manifest_path))
    package_root = manifest["packageRoot"]
    report = Report(manifest_path, package_root)
    log("manifest %s: %d asset(s) -> %s%s" % (
        manifest_path, len(manifest["assets"]), package_root, " (forced)" if force else ""))

    # A fresh commandlet has not indexed the mount; the stamps every reuse decision reads live on
    # the registry, and the background start-up scan owns the path, so the scan is forced.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [package_root], force_rescan=True)

    import_entries(manifest, staging_root, Tracker(force), report)
    try:
        protected = {entry["assetPath"] for entry in manifest["assets"]} | set(manifest["keep"])
        report.pruned = prune(package_root, protected, manifest["pruneScope"], report.ownership)
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
    manifest_path = cmdline_arg("ImportSurfaceProperties", "")
    if not manifest_path:
        raise SystemExit(
            "[import-surface-properties] -ImportSurfaceProperties=<manifest.json> is required")
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
