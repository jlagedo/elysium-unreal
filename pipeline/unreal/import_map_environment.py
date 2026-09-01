"""Phase 2 of `uv run elysium import map-environment`: land one staged environment per map as a
`UElysiumMapEnvironment` data asset under `/ElysiumBaked/<map>/DA_<map>_Environment`.

Runs inside a headless editor (`-run=pythonscript -script=pipeline/unreal/import_map_environment.py
-ImportMapEnvironment=<manifest.json>`). The offline stage (`importers/map_environment.py`, R4.4)
already read `<map>.env`, `<map>.sky` and `<map>.spawn` and asserted parity against them; this
script only turns those values into reflected properties and saves them
(`docs/architecture/seam_map_map.md` -> "Import — environment").

Nothing is decided here. Every value written below is copied from the manifest row verbatim -- the
asset is a transport change and nothing else, so a transformation in this file would be a divergence
between the two paths the runtime can take.

  * per map, compare the manifest recipe against the stamp the asset carries
    (`bake_lib.RECIPE_TAG`) and touch only what is new, changed or forced;
  * write `import_report.json` beside the manifest and exit non-zero if any map failed.

Command line:
  -ImportMapEnvironment=<path>  the manifest (required)
  -ImportForce=1                re-author every map regardless of stamp (`0`/`false`/`no` = off)
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

#: The recipe stage label every fingerprint hashes under -- the lane's own name.
STAGE = "map_environment"

#: Manifest schema this script understands.
MANIFEST_SCHEMA = "1.0.0"

#: The one class this lane authors.
ASSET_CLASS = "ElysiumMapEnvironment"


def log(msg):
    unreal.log("[import-map-environment] %s" % msg)


def fail(msg):
    unreal.log_error("[import-map-environment] %s" % msg)


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line (a quoted path is one token)."""
    needle = "-%s=" % key
    for token in re.findall(r'"[^"]*"|\S+', unreal.SystemLibrary.get_command_line()):
        token = token.strip('"')
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def flag(value):
    """A command-line switch as a boolean (`bool("0")` is True, so this is not a truthiness test)."""
    return str(value).strip().strip('"').lower() in ("1", "true", "yes", "on")


class ManifestError(RuntimeError):
    """The manifest cannot be executed as written."""


def load_manifest(path):
    with open(path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    if not isinstance(manifest, dict):
        raise ManifestError("manifest is not an object")
    if manifest.get("schemaVersion") != MANIFEST_SCHEMA:
        raise ManifestError("manifest schema %r is not %s"
                            % (manifest.get("schemaVersion"), MANIFEST_SCHEMA))
    entries = manifest.get("maps")
    if not isinstance(entries, list):
        raise ManifestError("manifest maps is not a list")
    mount = manifest.get("mount")
    if not isinstance(mount, str) or not mount.startswith("/") or mount.endswith("/"):
        raise ManifestError("manifest mount %r is not a mount path" % (mount,))
    for index, entry in enumerate(entries):
        for key in ("map", "assetPath", "packageRoot", "env", "sky", "spawn", "parity"):
            if key not in entry:
                raise ManifestError("maps[%d] lacks %r" % (index, key))
        if not entry["assetPath"].startswith(mount + "/"):
            raise ManifestError("maps[%d] %s is outside %s" % (index, entry["assetPath"], mount))
        # The stage refuses to write an entry whose parity failed, so an unchecked or unequal
        # row reaching this process is a manifest that must not be executed.
        parity = entry["parity"]
        if not parity.get("checked") or not parity.get("equal"):
            raise ManifestError("maps[%d] %s carries no passing parity verdict: %r"
                                % (index, entry["map"], parity))
    return manifest


def split_asset_path(asset_path):
    """`/Root/dir/DA_name` -> (`/Root/dir`, `DA_name`)."""
    package, _, name = asset_path.rpartition("/")
    return package, name


def make_vector(triple):
    return unreal.Vector(float(triple[0]), float(triple[1]), float(triple[2]))


def make_color(triple):
    return unreal.LinearColor(float(triple[0]), float(triple[1]), float(triple[2]), 1.0)


def author_map(entry, force=False):
    """Author or reuse one map's `UElysiumMapEnvironment`. Returns "imported" or "reused"."""
    object_path = entry["assetPath"]
    package_root, asset_name = split_asset_path(object_path)
    env, sky, spawn = entry["env"], entry["sky"], entry["spawn"]

    # The fingerprint covers every value the asset carries, so a re-stage that moves the sky
    # miniature rebuilds and a re-stage that changes nothing does not.
    fingerprint = bl.recipe_fingerprint(
        STAGE, object_path,
        {"recipeVersion": entry.get("recipeVersion"), "env": env, "sky": sky, "spawn": spawn},
    )
    if (not force and unreal.EditorAssetLibrary.does_asset_exist(object_path)
            and bl.asset_class_name(object_path) == ASSET_CLASS
            and bl.stored_recipe(object_path) == fingerprint):
        return "reused"

    bl.ensure_dir(package_root)
    asset = unreal.load_asset(object_path)
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.ElysiumMapEnvironment)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_root, unreal.ElysiumMapEnvironment, factory)
    if asset is None:
        raise RuntimeError("could not create %s" % object_path)

    asset.set_editor_property("map_name", entry["map"])

    asset.set_editor_property("sky", bool(env["sky"]))
    asset.set_editor_property("sky_name", env["skyName"])
    asset.set_editor_property("sky_convention", int(env["skyConvention"]))
    asset.set_editor_property("fog", bool(env["fog"]))
    asset.set_editor_property("fog_color", make_color(env["fogColor"]))
    asset.set_editor_property("fog_start_cm", float(env["fogStart"]))
    asset.set_editor_property("fog_end_cm", float(env["fogEnd"]))
    asset.set_editor_property("sky_fog", bool(env["skyFog"]))
    asset.set_editor_property("sky_fog_color", make_color(env["skyFogColor"]))
    asset.set_editor_property("sky_fog_start_cm", float(env["skyFogStart"]))
    asset.set_editor_property("sky_fog_end_cm", float(env["skyFogEnd"]))

    asset.set_editor_property("has_sky_miniature", bool(sky["hasMiniature"]))
    asset.set_editor_property("sky_origin_cm", make_vector(sky["origin"]))
    asset.set_editor_property("sky_scale", float(sky["scale"]))

    asset.set_editor_property("has_spawn", bool(spawn["hasSpawn"]))
    asset.set_editor_property("spawn_origin_cm", make_vector(spawn["origin"]))
    asset.set_editor_property("spawn_yaw_deg", float(spawn["yaw"]))

    bl.stamp_recipe(asset, fingerprint)
    if not bl.save(object_path):
        raise RuntimeError("save failed: %s" % object_path)
    return "imported"


def run(manifest_path, force=False):
    manifest = load_manifest(manifest_path)
    staging_root = os.path.dirname(os.path.abspath(manifest_path))
    entries = manifest["maps"]
    log("manifest %s: %d map(s)%s" % (manifest_path, len(entries), " (forced)" if force else ""))

    # A fresh commandlet has not indexed the mount; the stamps every reuse decision reads live on
    # the registry.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [manifest["mount"]], force_rescan=True)

    report = {
        "manifest": manifest_path,
        "imported": 0,
        "reused": 0,
        "failed": [],
        "maps": [],
        "stageFailures": manifest.get("stageFailures", []),
    }
    for entry in entries:
        started = time.time()
        try:
            outcome = author_map(entry, force=force)
        except Exception as exc:  # noqa: BLE001 - one map's failure is not the run's
            report["failed"].append({"map": entry["map"], "assetPath": entry["assetPath"],
                                     "reason": "%s" % exc})
            fail("%s: %s" % (entry["map"], exc))
            unreal.log_error(traceback.format_exc())
            continue
        report[outcome] += 1
        report["maps"].append({
            "map": entry["map"],
            "assetPath": entry["assetPath"],
            "hasSkyMiniature": entry["sky"]["hasMiniature"],
            "hasSpawn": entry["spawn"]["hasSpawn"],
            "outcome": outcome,
            "seconds": round(time.time() - started, 2),
        })
        log("%s: sky miniature=%s spawn=%s %s -> %s"
            % (entry["map"], entry["sky"]["hasMiniature"], entry["spawn"]["hasSpawn"], outcome,
               entry["assetPath"]))

    report_path = os.path.join(staging_root, "import_report.json")
    with open(report_path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
    log("%d imported / %d reused / %d failed -> %s"
        % (report["imported"], report["reused"], len(report["failed"]), report_path))
    return report


def main():
    manifest_path = cmdline_arg("ImportMapEnvironment", "")
    if not manifest_path:
        raise SystemExit(
            "[import-map-environment] -ImportMapEnvironment=<manifest.json> is required")
    force = flag(cmdline_arg("ImportForce", ""))
    try:
        report = run(manifest_path, force=force)
    except (ManifestError, OSError, ValueError) as exc:
        fail("manifest refused: %s" % exc)
        raise SystemExit(1)
    if report["failed"]:
        fail("%d map(s) failed; see import_report.json" % len(report["failed"]))
        raise SystemExit(1)


main()
