"""Phase 2 of `uv run elysium bake sounds`: land the staged audio as `USoundWave` assets.

Runs inside a headless editor (`-run=pythonscript -script=pipeline/unreal/import_sounds.py
-ImportSounds=<manifest.json>`). The offline stage
(`elysium_pipeline.importers.sounds_bake`) already decided everything -- asset path, which file to
import, the loop trim and `bLooping`, the compression -- and wrote it to the manifest this script
reads. This script only executes those decisions against the editor:

  * per entry, compare the manifest's recipe against the stamp the asset carries
    (`bake_lib.RECIPE_TAG`) and import only what is new, changed or forced;
  * import in chunks through `AssetImportTask`, apply the settings, stamp, save;
  * prune every asset inside the manifest's `pruneScope` folder the manifest neither names nor
    protects -- and prune nothing at all when the run was scoped to a key selection, since a
    subset run knows nothing about the assets it did not stage;
  * write `import_report.json` beside the manifest and exit non-zero if any entry failed.

Failures are isolated per entry: an exception names the entry in the report and the run goes on,
so a defect late in a 10k-unit run costs one relaunch (which resumes from the stamps), not the
run.

Command line:
  -ImportSounds=<path>   the manifest (required)
  -ImportForce=1         re-import every entry regardless of stamp (`0`/`false`/`no` = off)
"""
from __future__ import annotations

import gc
import json
import os
import re
import sys
import time
import traceback

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402

#: The recipe stage label every fingerprint hashes under, and the producer tag every asset
#: carries so `prune_owned` never touches another lane's package.
STAGE = "sounds"
PRODUCER = "sounds"

#: Manifest schema this script understands.
MANIFEST_SCHEMA = "1.0.0"

#: Entries per `import_asset_tasks` call; garbage is collected between chunks so the raw PCM of a
#: finished chunk does not accumulate across a 10k-asset run. Sized like the texture lane's; see
#: `import_textures._collect_garbage` for how to re-derive it from a full-corpus run.
CHUNK = 64

_tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("[bake-sounds] %s" % msg)


def warn(msg):
    unreal.log_warning("[bake-sounds] %s" % msg)


def fail(msg):
    unreal.log_error("[bake-sounds] %s" % msg)


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line, tolerating a quoted path with spaces."""
    needle = "-%s=" % key
    for token in re.findall(r'"[^"]*"|\S+', unreal.SystemLibrary.get_command_line()):
        token = token.strip('"')
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def flag(value):
    """A command-line switch as a boolean: `bool("0")` is True, so this is not a truthiness test."""
    return str(value).strip().strip('"').lower() in ("1", "true", "yes", "on")


def _collect_garbage():
    """Free what the finished chunk no longer holds, synchronously and now.

    `unreal.collect_garbage` (PythonScriptPlugin) calls `::CollectGarbage` on the spot, unlike
    `SystemLibrary.collect_garbage`, which only raises a flag `UWorld::Tick` consumes and a
    commandlet never reaches. Python's own cycle pass runs first: an `unreal` wrapper is a root
    for the collector while it lives.
    """
    gc.collect()
    unreal.collect_garbage()


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
    if not isinstance(root, str) or not root.startswith("/") or root.endswith("/"):
        raise ManifestError("manifest packageRoot %r is not a mount path" % (root,))
    entries = manifest.get("assets")
    if not isinstance(entries, list):
        raise ManifestError("manifest assets is not a list")
    scope = manifest.get("pruneScope", root + "/")
    if scope is not None and (not isinstance(scope, str) or not scope.endswith("/")
                              or not scope.lower().startswith((root + "/").lower())):
        raise ManifestError("manifest pruneScope %r is neither null nor a folder below %s"
                            % (scope, root))
    manifest["pruneScope"] = scope
    keep = manifest.get("keep", [])
    if not isinstance(keep, list) or not all(isinstance(path, str) and path.startswith(root + "/")
                                             for path in keep):
        raise ManifestError("manifest keep is not a list of asset paths below %s" % root)
    manifest["keep"] = keep
    seen = set()
    for index, entry in enumerate(entries):
        for key in ("assetPath", "class", "file", "unit", "recipe", "settings", "looping"):
            if key not in entry:
                raise ManifestError("assets[%d] lacks %r" % (index, key))
        if not entry["assetPath"].startswith(root + "/"):
            raise ManifestError("assets[%d] %s is outside %s" % (index, entry["assetPath"], root))
        if entry["class"] != "SoundWave":
            raise ManifestError("assets[%d] class %r is not SoundWave" % (index, entry["class"]))
        if entry["assetPath"] in seen:
            raise ManifestError("assets[%d] %s is listed twice" % (index, entry["assetPath"]))
        seen.add(entry["assetPath"])
    return manifest


def split_asset_path(asset_path):
    """`/Root/dir/SW_name` -> (`/Root/dir`, `SW_name`)."""
    package, _, name = asset_path.rpartition("/")
    return package, name


# --- settings -----------------------------------------------------------------------------------


def settings_for(entry):
    """The editor properties one manifest entry asks for, as {property: enum-or-value}.

    Pure over the entry so a test can check the mapping without an editor. Everything the owner
    call did not name stays at the engine default: only the loop flag and the compression type
    are ours. `looping` is `USoundWave::bLooping`, which is what makes a trimmed loop body wrap
    instead of stopping; `sound_asset_compression_type` is `ProjectDefined`, so the codec is the
    one decision in project settings rather than 10,892 decisions in a manifest.
    """
    settings = dict(entry.get("settings") or {})
    compression = str(settings.get("compression", "ProjectDefined"))
    try:
        codec = getattr(unreal.SoundAssetCompressionType, {
            "ProjectDefined": "PROJECT_DEFINED",
            "PlatformSpecific": "PLATFORM_SPECIFIC",
            "BinkAudio": "BINK_AUDIO",
            "RADAudio": "RAD_AUDIO",
            "ADPCM": "ADPCM",
            "PCM": "PCM",
            "Opus": "OPUS",
        }[compression])
    except KeyError:
        raise RuntimeError("unknown compression %r" % compression)
    return {"looping": bool(entry.get("looping")), "sound_asset_compression_type": codec}


def apply_settings(wave, entry):
    """One plural `set_editor_properties` call, so the whole change is bracketed by one
    `PreEditChange`/`PostEditChange` pair rather than one per property -- a sound wave's
    `PostEditChangeProperty` invalidates its compressed data, so per-property writes cost one
    re-encode each."""
    wave.set_editor_properties(settings_for(entry))


def class_problem(wave, entry):
    name = wave.get_class().get_name()
    return None if name == entry["class"] else "imported as %s, not %s" % (name, entry["class"])


# --- the run -------------------------------------------------------------------------------------


class Tracker(object):
    """Per-asset reuse: the manifest recipe against the stamp the asset carries.

    The same decision the texture lane makes: force rebuilds; a missing asset builds; an asset of
    the wrong class is deleted and rebuilt; a stamp equal to the recipe fingerprint is reused.
    """

    def __init__(self, force=False, ledger=None):
        self.force = bool(force)
        self.fingerprints = {}
        self.ledger = ledger

    def fingerprint(self, entry):
        path = entry["assetPath"]
        if path not in self.fingerprints:
            self.fingerprints[path] = bl.recipe_fingerprint(STAGE, path, entry["recipe"])
            if self.ledger is not None:
                self.ledger.record(path, entry["recipe"])
        return self.fingerprints[path]

    def needs_import(self, entry):
        path = entry["assetPath"]
        fingerprint = self.fingerprint(entry)
        stored = bl.stored_recipe(path, producer=PRODUCER)
        exists = unreal.EditorAssetLibrary.does_asset_exist(path)
        if exists and bl.asset_class_name(path) != entry["class"]:
            bl.delete_owned_asset(path)
            exists = False
        if self.force or not exists:
            return True
        if stored != fingerprint and self.ledger is not None:
            self.ledger.explain(path, entry["recipe"], stored)
        return stored != fingerprint


class Report(object):
    def __init__(self, manifest_path, package_root, select=None, empty=(), placeholders=()):
        self.manifest = manifest_path
        self.package_root = package_root
        self.select = select
        self.empty = list(empty)
        self.placeholders = list(placeholders)
        self.built = 0
        self.reused = 0
        self.pruned = 0
        self.ownership = {"foreign": 0, "unstamped": 0}
        self.loops = {}
        self.failures = []
        self.started = time.time()

    def failed(self, entry, reason):
        self.failures.append({"assetPath": entry["assetPath"], "unit": entry.get("unit", ""),
                              "reason": reason})
        fail("%s: %s" % (entry["assetPath"], reason))

    def as_dict(self):
        """The `import_report.json` body. `imported`, `reused`, `pruned`, `empty` and `failed`
        (a list of `{assetPath, unit, reason}`) are what the CLI reads back."""
        return {
            "schemaVersion": "1.0.0",
            "manifest": self.manifest,
            "packageRoot": self.package_root,
            "select": self.select,
            "imported": self.built,
            "reused": self.reused,
            "pruned": self.pruned, **self.ownership,
            "empty": self.empty,
            "placeholders": self.placeholders,
            "loops": self.loops,
            "failed": self.failures,
            "seconds": round(time.time() - self.started, 1),
        }

    def summary(self):
        return ("%d built, %d reused, %d pruned, %d empty, %d placeholder, %d failed"
                % (self.built, self.reused, self.pruned, len(self.empty),
                   len(self.placeholders), len(self.failures)))


def _import_chunk(entries, staging_root):
    """Import one chunk of entries; returns {assetPath: sound wave or None}."""
    tasks = []
    for entry in entries:
        package, name = split_asset_path(entry["assetPath"])
        bl.ensure_dir(package)
        task = unreal.AssetImportTask()
        task.filename = os.path.join(staging_root, entry["file"].replace("/", os.sep))
        task.destination_path = package
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.replace_existing_settings = True
        task.save = False
        tasks.append(task)
    if tasks:
        _tools.import_asset_tasks(tasks)
    return {entry["assetPath"]: unreal.EditorAssetLibrary.load_asset(entry["assetPath"])
            for entry in entries}


def _finish_entry(wave, entry, tracker, report):
    """Everything after the import call for one entry; raises on a defect."""
    if wave is None:
        raise RuntimeError("import produced no asset")
    problem = class_problem(wave, entry)
    if problem:
        raise RuntimeError(problem)
    apply_settings(wave, entry)
    bl.stamp_recipe(wave, tracker.fingerprint(entry), producer=PRODUCER)
    if not bl.save(entry["assetPath"]):
        raise RuntimeError("save failed")
    decision = str((entry.get("loop") or {}).get("decision", "none"))
    report.loops[decision] = report.loops.get(decision, 0) + 1
    report.built += 1


def import_entries(manifest, staging_root, tracker, report):
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
                _finish_entry(imported.get(entry["assetPath"]), entry, tracker, report)
            except Exception as exc:  # noqa: BLE001
                report.failed(entry, "%s" % exc)
                if not isinstance(exc, RuntimeError):
                    warn(traceback.format_exc())
        _collect_garbage()
        log("[%d/%d] %s" % (min(start + CHUNK, len(pending)), len(pending), report.summary()))


def prune(package_root, keep, scope, counts=None):
    return bl.prune_owned(package_root, keep, scope, PRODUCER, counts)


def run(manifest_path, force=False):
    manifest = load_manifest(manifest_path)
    staging_root = os.path.dirname(os.path.abspath(manifest_path))
    package_root = manifest["packageRoot"]
    select = manifest.get("select")
    report = Report(manifest_path, package_root, select, manifest.get("empty") or [],
                    manifest.get("placeholders") or [])
    log("manifest %s: %d asset(s) -> %s%s%s" % (
        manifest_path, len(manifest["assets"]), package_root,
        " (select %d key(s))" % len(select) if select else "",
        " (forced)" if force else ""))

    # A fresh commandlet has not indexed the mount; the stamps every reuse decision reads live on
    # the registry, and the background start-up scan owns the path, so the scan is forced.
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([package_root], force_rescan=True)

    ledger = bl.RecipeLedger(os.path.join(staging_root, "recipes.json"), "bake-sounds")
    import_entries(manifest, staging_root, Tracker(force, ledger), report)
    ledger.write()
    if manifest["pruneScope"] is None:
        log("scoped run: nothing pruned")
    else:
        try:
            protected = {entry["assetPath"] for entry in manifest["assets"]} | set(manifest["keep"])
            report.pruned = prune(package_root, protected, manifest["pruneScope"],
                                  report.ownership)
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
    manifest_path = cmdline_arg("ImportSounds", "")
    if not manifest_path:
        raise SystemExit("[bake-sounds] -ImportSounds=<manifest.json> is required")
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
