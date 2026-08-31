"""Phase 2 of `uv run elysium import textures`: land the staged DDS set as Unreal texture assets.

Runs inside a headless editor (`-run=pythonscript -script=pipeline/unreal/import_textures.py
-ImportTextures=<manifest.json>`). The offline stage already decided everything -- asset path,
class, sRGB, compression, mips, sampling -- and wrote it to the manifest this script reads
(`docs/architecture/seam_map_texture.md` -> "Import" -> "The staging manifest"). This script
only executes those decisions against the editor:

  * per entry, compare the manifest's recipe against the stamp the asset carries
    (`bake_lib.RECIPE_TAG`) and import only what is new, changed or forced;
  * import in chunks through `AssetImportTask`, apply the settings, attach the provenance record
    (`UElysiumTextureProvenance.apply_json`), publish its registry tags, stamp, save;
  * verify what Unreal built against `expected`, recording a short authored chain Unreal
    extended rather than failing it;
  * with `-ImportMeasure=1`, write each built mip 0 back beside its staged DDS as
    `<stem>.built.dds` for the offline measure phase;
  * prune every asset inside the manifest's `pruneScope` folder that the manifest neither
    names (`assets`) nor protects (`keep`: the asset paths of units the stage could not read),
    so a partial run never empties the rest and a transient stage failure never deletes a
    previously good asset;
  * write `import_report.json` beside the manifest and exit non-zero if any entry failed.

Failures are isolated per entry: an exception names the entry in the report and the run goes on,
so a defect late in an 11k-unit run costs one relaunch (which resumes from the stamps), not the
run. The first run is the whole corpus by owner call (`docs/project/seam_migration.md`).

Command line:
  -ImportTextures=<path>   the manifest (required)
  -ImportForce=1           re-import every entry regardless of stamp (`0`/`false`/`no` = off)
  -ImportMeasure=1         write `<stem>.built.dds` for each non-twin entry (same spelling)
"""
from __future__ import annotations

import json
import os
import re
import sys
import time
import traceback

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402

#: The recipe stage label every fingerprint hashes under. The shared-corpus bake stamps its own
#: `textures` stage too, but a fingerprint also hashes the object path and the two lanes own
#: different package roots, so no stamp can be mistaken for the other lane's.
STAGE = "textures"

#: Manifest schema this script understands.
MANIFEST_SCHEMA = "1.0.0"

#: Entries per `import_asset_tasks` call; garbage is collected between chunks so the platform
#: data of a finished chunk does not accumulate across an 11k-asset run.
CHUNK = 64

#: Assets per `delete_loaded_assets` call while pruning.
PRUNE_CHUNK = 256

_tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("[import-textures] %s" % msg)


def warn(msg):
    unreal.log_warning("[import-textures] %s" % msg)


def fail(msg):
    unreal.log_error("[import-textures] %s" % msg)


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line.

    A quoted token (`"-ImportTextures=C:/path with spaces/manifest.json"`) is one token: the
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
    seen = set()
    for index, entry in enumerate(entries):
        for key in ("assetPath", "class", "dds", "provenance", "unit", "recipe", "expected"):
            if key not in entry:
                raise ManifestError("assets[%d] lacks %r" % (index, key))
        if not entry["assetPath"].startswith(root + "/"):
            raise ManifestError("assets[%d] %s is outside %s" % (index, entry["assetPath"], root))
        if entry["class"] not in ("Texture2D", "TextureCube", "Texture2DArray"):
            raise ManifestError("assets[%d] class %r unknown" % (index, entry["class"]))
        if entry["assetPath"] in seen:
            raise ManifestError("assets[%d] %s is listed twice" % (index, entry["assetPath"]))
        seen.add(entry["assetPath"])
    return manifest


def split_asset_path(asset_path):
    """`/Root/dir/T_name` -> (`/Root/dir`, `T_name`)."""
    package, _, name = asset_path.rpartition("/")
    return package, name


# --- settings -----------------------------------------------------------------------------------


def settings_for(entry):
    """The editor properties one manifest entry asks for, as {property: enum-or-value}.

    Pure over the entry so a test can check the mapping without an editor; `apply_settings`
    only sets what this returns. `address_x`/`address_y` exist on Texture2D and Texture2DArray,
    not on TextureCube, so a cube entry never asks for them.
    """
    srgb = bool(entry.get("srgb", True))
    compression = entry.get("compression", "default")
    if compression == "default":
        tc = unreal.TextureCompressionSettings.TC_DEFAULT
    elif compression == "uncompressed":
        tc = (unreal.TextureCompressionSettings.TC_EDITOR_ICON if srgb
              else unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    else:
        raise ManifestError("%s: compression %r unknown" % (entry["assetPath"], compression))

    mip_gen = entry.get("mipGen", "leave-existing")
    if mip_gen == "leave-existing":
        mg = unreal.TextureMipGenSettings.TMGS_LEAVE_EXISTING_MIPS
    elif mip_gen == "no-mipmaps":
        mg = unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS
    else:
        raise ManifestError("%s: mipGen %r unknown" % (entry["assetPath"], mip_gen))

    filters = {
        "default": unreal.TextureFilter.TF_DEFAULT,
        "nearest": unreal.TextureFilter.TF_NEAREST,
        "trilinear": unreal.TextureFilter.TF_TRILINEAR,
    }
    filter_name = entry.get("filter", "default")
    if filter_name not in filters:
        raise ManifestError("%s: filter %r unknown" % (entry["assetPath"], filter_name))

    settings = {
        "srgb": srgb,
        "compression_settings": tc,
        "lossy_compression_amount": unreal.TextureLossyCompressionAmount.TLCA_NONE,
        "mip_gen_settings": mg,
        "filter": filters[filter_name],
        "never_stream": bool(entry.get("neverStream", False)),
    }
    if entry["class"] in ("Texture2D", "Texture2DArray"):
        addresses = {"wrap": unreal.TextureAddress.TA_WRAP, "clamp": unreal.TextureAddress.TA_CLAMP}
        for key, prop in (("addressX", "address_x"), ("addressY", "address_y")):
            mode = entry.get(key, "wrap")
            if mode not in addresses:
                raise ManifestError("%s: %s %r unknown" % (entry["assetPath"], key, mode))
            settings[prop] = addresses[mode]
    return settings


def apply_settings(texture, entry):
    for prop, value in settings_for(entry).items():
        texture.set_editor_property(prop, value)


# --- verification --------------------------------------------------------------------------------


def class_problem(texture, entry):
    """The failure string when the import produced the wrong asset class, else None."""
    if texture.get_class().get_name() != entry["class"]:
        return "imported as %s, expected %s" % (texture.get_class().get_name(), entry["class"])
    return None


def verify_built(texture, entry):
    """Compare what Unreal built with the manifest's `expected`.

    Called **after** `apply_settings`: reading the built extent blocks on a texture build, and
    the build must be the one the manifest asked for (its mip policy and compression), not the
    factory's defaults -- otherwise a single-level unit would build a full chain under
    `TMGS_FromTextureGroup`, be recorded as a false short chain, and be encoded twice.

    Returns (problem, short_chain): `problem` is a string when the asset must count as failed,
    `short_chain` is {authoredMips, builtMips} when Unreal built more levels than the unit
    authored -- the 65 chains that end above 1x1 -- which is recorded, not failed. A cubemap's
    slice count is not held against `expected`: the platform data does not report faces as
    slices, and the class check already proves the cube.
    """
    expected = entry["expected"]
    problem = class_problem(texture, entry)
    if problem:
        return problem, None
    width, height, slices, mips = unreal.ElysiumTextureImportLibrary.built_extent(texture)
    if (width, height) != (expected["width"], expected["height"]):
        return "built %dx%d, expected %dx%d" % (width, height, expected["width"], expected["height"]), None
    if entry["class"] == "Texture2DArray" and slices != expected.get("slices", 1):
        return "built %d slices, expected %d" % (slices, expected.get("slices", 1)), None
    authored = expected["mips"]
    if mips < authored:
        return "built %d mips, unit authored %d" % (mips, authored), None
    if mips > authored:
        return None, {"authoredMips": authored, "builtMips": mips}
    return None, None


# --- the run -------------------------------------------------------------------------------------


class Tracker(object):
    """Per-asset reuse: the manifest recipe against the stamp the asset carries.

    The same decision `bake_map.AssetTracker.register` makes, without that module's map-bake
    surface: force rebuilds; a missing asset builds; an asset of the wrong class is deleted and
    rebuilt; a stamp equal to the recipe fingerprint is reused.
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
        if exists and bl.asset_class_name(path) != entry["class"]:
            bl.delete_owned_asset(path)
            exists = False
        if self.force or not exists:
            return True
        return bl.stored_recipe(path) != fingerprint


class Report(object):
    def __init__(self, manifest_path, package_root, select=None):
        self.manifest = manifest_path
        self.package_root = package_root
        self.select = select
        self.built = 0
        self.reused = 0
        self.pruned = 0
        self.measured = 0
        self.failures = []
        self.short_chains = []
        self.built_formats = {}
        self.measure_skipped = []
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
            "measured": self.measured,
            "failed": self.failures,
            "shortChains": self.short_chains,
            "builtFormats": self.built_formats,
            "measureSkipped": self.measure_skipped,
            "seconds": round(time.time() - self.started, 1),
        }

    def summary(self):
        return ("%d built, %d reused, %d pruned, %d measured, %d failed"
                % (self.built, self.reused, self.pruned, self.measured, len(self.failures)))


def _import_chunk(entries, staging_root):
    """Import one chunk of entries; returns {assetPath: texture or None}."""
    tasks = []
    for entry in entries:
        package, name = split_asset_path(entry["assetPath"])
        bl.ensure_dir(package)
        task = unreal.AssetImportTask()
        task.filename = os.path.join(staging_root, entry["dds"].replace("/", os.sep))
        task.destination_path = package
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.replace_existing_settings = True
        task.save = False
        tasks.append(task)
    if tasks:
        _tools.import_asset_tasks(tasks)
    return {entry["assetPath"]: unreal.EditorAssetLibrary.load_asset(entry["assetPath"]) for entry in entries}


def _built_dds_path(staging_root, entry):
    """`hud/signs/notepad_yellow.dds` -> `<staging>/hud/signs/notepad_yellow.built.dds`."""
    rel = entry["dds"]
    stem = rel[:-4] if rel.lower().endswith(".dds") else rel
    return os.path.join(staging_root, (stem + ".built.dds").replace("/", os.sep))


def _finish_entry(texture, entry, staging_root, tracker, report, measure):
    """Everything after the import call for one entry; raises on a defect."""
    if texture is None:
        raise RuntimeError("import produced no asset")
    problem = class_problem(texture, entry)
    if problem:
        raise RuntimeError(problem)
    # Settings first: the verification below forces the build, and the build must be the one the
    # manifest asked for.
    apply_settings(texture, entry)
    problem, short_chain = verify_built(texture, entry)
    if problem:
        raise RuntimeError(problem)

    with open(os.path.join(staging_root, entry["provenance"].replace("/", os.sep)), "r",
              encoding="utf-8") as handle:
        sidecar = handle.read()
    record, error = unreal.ElysiumTextureProvenance.apply_json(texture, sidecar)
    if record is None:
        raise RuntimeError("provenance rejected: %s" % error)
    stamped, error = unreal.ElysiumTextureProvenance.stamp_registry_tags(texture)
    if not stamped:
        raise RuntimeError("registry tags: %s" % error)

    bl.stamp_recipe(texture, tracker.fingerprint(entry))
    if not bl.save(entry["assetPath"]):
        raise RuntimeError("save failed")

    built_format = unreal.ElysiumTextureImportLibrary.built_pixel_format(texture)
    report.built_formats[built_format] = report.built_formats.get(built_format, 0) + 1
    if short_chain:
        short_chain = dict(short_chain, assetPath=entry["assetPath"], builtFormat=built_format)
        report.short_chains.append(short_chain)

    # A twin shares its DDS with the colour asset, and the re-encode is decided by the blocks and
    # the compression preset, both shared -- so the colour asset's measurement stands for both.
    if measure and not entry.get("twinOf"):
        written, error = unreal.ElysiumTextureImportLibrary.write_built_mip_zero_as_dds(
            texture, _built_dds_path(staging_root, entry))
        if written:
            report.measured += 1
        else:
            report.measure_skipped.append({"assetPath": entry["assetPath"], "reason": error})
    report.built += 1


def import_entries(manifest, staging_root, tracker, report, measure):
    """Decide, import in chunks, finish each; every defect lands in the report."""
    pending = []
    for entry in manifest["assets"]:
        try:
            if tracker.needs_import(entry):
                pending.append(entry)
            else:
                report.reused += 1
        except Exception as exc:  # noqa: BLE001 - isolated per entry by design
            report.failed(entry, "reuse check raised: %s" % exc)
    log("%d to import, %d reused" % (len(pending), report.reused))

    for start in range(0, len(pending), CHUNK):
        chunk = pending[start:start + CHUNK]
        try:
            imported = _import_chunk(chunk, staging_root)
        except Exception as exc:  # noqa: BLE001
            for entry in chunk:
                report.failed(entry, "import call raised: %s" % exc)
            _collect_garbage()
            continue
        for entry in chunk:
            try:
                _finish_entry(imported.get(entry["assetPath"]), entry, staging_root, tracker, report, measure)
            except Exception as exc:  # noqa: BLE001
                report.failed(entry, "%s" % exc)
                if not isinstance(exc, RuntimeError):
                    warn(traceback.format_exc())
        _collect_garbage()
        log("[%d/%d] %s" % (min(start + CHUNK, len(pending)), len(pending), report.summary()))


def prune(package_root, keep, scope):
    """Delete every asset inside `scope` the manifest neither names nor protects, then empty folders.

    Returns the number of assets deleted. `keep` holds package paths (`/Root/dir/T_name`): the
    manifest's `assets` plus its `keep` list. `scope` is the manifest's `pruneScope` -- the whole
    root, or the folded package folder of the selected directory, always with a trailing slash --
    compared case-insensitively, so `hud/` never reaches `hudson/` and a manifest that names only
    part of the corpus never empties the namespace an earlier full run filled.
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
    # Folders inside the scope left empty by the deletions, deepest first so a parent empties
    # after its children.
    folders = [path for path in library.list_assets(package_root, recursive=True, include_folder=True)
               if path.endswith("/") and path.lower().startswith(scope)]
    for folder in sorted(folders, key=lambda p: p.count("/"), reverse=True):
        if not library.list_assets(folder, recursive=True, include_folder=False):
            library.delete_directory(folder)
    return len(stale)


def run(manifest_path, force=False, measure=True):
    manifest = load_manifest(manifest_path)
    staging_root = os.path.dirname(os.path.abspath(manifest_path))
    package_root = manifest["packageRoot"]
    select = manifest.get("select")
    report = Report(manifest_path, package_root, select)
    log("manifest %s: %d asset(s) -> %s%s%s%s" % (
        manifest_path, len(manifest["assets"]), package_root,
        " (select %s)" % select if select else "",
        " (forced)" if force else "", " (measuring)" if measure else ""))

    # A fresh commandlet has not indexed the mount; the stamps every reuse decision reads live on
    # the registry, and the background start-up scan owns the path, so the scan is forced.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([package_root], force_rescan=True)

    import_entries(manifest, staging_root, Tracker(force), report, measure)
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
    manifest_path = cmdline_arg("ImportTextures", "")
    if not manifest_path:
        raise SystemExit("[import-textures] -ImportTextures=<manifest.json> is required")
    force = flag(cmdline_arg("ImportForce", ""))
    measure = flag(cmdline_arg("ImportMeasure", ""))
    try:
        report = run(manifest_path, force=force, measure=measure)
    except (ManifestError, OSError, ValueError) as exc:
        # A manifest that cannot be read, parsed or executed is one refusal, not a traceback.
        fail("manifest refused: %s" % exc)
        raise SystemExit(1)
    if report.failures:
        fail("%d entry(ies) failed; see import_report.json" % len(report.failures))
        raise SystemExit(1)


main()
