"""Phase 2 of `uv run elysium import map-entities`: land one staged entity table per map as a
`UElysiumMapEntities` data asset under `/ElysiumBaked/Maps/<map>/DA_<map>_Entities`.

Runs inside a headless editor (`-run=pythonscript -script=pipeline/unreal/import_map_entities.py
-ImportMapEntities=<manifest.json>`). The offline stage (`importers/map_entities.py`, R4.1) already
ran the R3.2 producer's entity join and asserted def-count and per-index parity against the
`<map>.ents` document the asset replaces; this script only turns those rows into reflected structs
and saves them.

Nothing is decided here. Every value written below is copied from the manifest row verbatim -- the
asset is a transport change and nothing else, so a transformation in this file would be a
divergence between the two paths the runtime can take.

  * per map, compare the manifest recipe against the stamp the asset carries
    (`bake_lib.RECIPE_TAG`) and touch only what is new, changed or forced;
  * write `import_report.json` beside the manifest and exit non-zero if any map failed.

Command line:
  -ImportMapEntities=<path>   the manifest (required)
  -ImportForce=1              re-author every map regardless of stamp (`0`/`false`/`no` = off)
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
STAGE = "map_entities"

#: Manifest schema this script understands.
MANIFEST_SCHEMA = "1.0.0"

#: The one class this lane authors.
ASSET_CLASS = "ElysiumMapEntities"


def log(msg):
    unreal.log("[import-map-entities] %s" % msg)


def fail(msg):
    unreal.log_error("[import-map-entities] %s" % msg)


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
        for key in ("map", "assetPath", "packageRoot", "entities", "parity"):
            if key not in entry:
                raise ManifestError("maps[%d] lacks %r" % (index, key))
        if not entry["assetPath"].startswith(mount + "/"):
            raise ManifestError("maps[%d] %s is outside %s" % (index, entry["assetPath"], mount))
        # The stage refuses to write an entry whose parity failed, so an unchecked or unequal row
        # set reaching this process is a manifest that must not be executed.
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


def make_output_row(row):
    out = unreal.ElysiumMapEntityOutputRow()
    out.set_editor_property("name", row.get("name", ""))
    out.set_editor_property("target", row.get("target", ""))
    out.set_editor_property("input", row.get("input", ""))
    out.set_editor_property("param", row.get("param", ""))
    out.set_editor_property("delay", float(row.get("delay", 0.0)))
    # Stored as authored: the `0` -> -1 (unlimited) rewrite is the C++ deserializer's, the same one
    # owner the `.ents` reader is (R3.4).
    out.set_editor_property("times", int(row.get("times", -1)))
    out.set_editor_property("python", row.get("python", ""))
    return out


def make_hull_row(flat):
    """One hull: the sidecar's flat `x y z x y z ...` list, whole triples only."""
    hull = unreal.ElysiumMapEntityHullRow()
    hull.set_editor_property(
        "vertices",
        [make_vector(flat[i:i + 3]) for i in range(0, len(flat) - 2, 3)],
    )
    return hull


def make_entity_row(row):
    """One `.ents` row as one `FElysiumMapEntityRow`, field for field, nothing derived."""
    out = unreal.ElysiumMapEntityRow()
    out.set_editor_property("classname", row.get("classname", ""))
    out.set_editor_property("target_name", row.get("targetname", ""))
    out.set_editor_property("origin", make_vector(row.get("origin", [0.0, 0.0, 0.0])))
    out.set_editor_property("keys", dict(row.get("keys", {})))
    # Absent `model` means a point entity: INDEX_NONE, which is what the JSON reader leaves.
    out.set_editor_property("model", int(row["model"]) if "model" in row else -1)
    out.set_editor_property("hulls", [make_hull_row(hull) for hull in row.get("hulls", [])])
    out.set_editor_property("contents", int(row.get("contents", 0)))
    out.set_editor_property("blocks_player", bool(row.get("blocks_player", False)))
    out.set_editor_property("brush_mesh", row.get("brush_mesh", ""))
    # R6.4: absent means "never culled by distance", which the C++ reader spells 0.
    out.set_editor_property("cull_max_cm", float(row.get("cull_max_cm", 0.0)))
    out.set_editor_property("elevator_floors",
                            [float(z) for z in row.get("elevator_floors", [])])
    out.set_editor_property("start_hidden", bool(row.get("start_hidden", False)))
    out.set_editor_property("sky", bool(row.get("sky", False)))
    out.set_editor_property("model_mesh", row.get("model_mesh", ""))
    # Four doubles, not an `unreal.Quat`: a reflected FQuat property comes back out of a saved
    # package at binary32 (0.707107 -> 0.7071070075035095), which would make the asset disagree
    # with the `.ents` document it must reproduce. See the header's own note.
    quat = row.get("model_quat") or [0.0, 0.0, 0.0, 1.0]
    for axis, value in zip("xyzw", quat):
        out.set_editor_property("model_quat_%s" % axis, float(value))
    out.set_editor_property("hinge_axis", make_vector(row.get("hinge_axis", [0.0, 0.0, 0.0])))
    out.set_editor_property("outputs",
                            [make_output_row(o) for o in row.get("outputs", [])])
    return out


def author_map(entry, force=False):
    """Author or reuse one map's `UElysiumMapEntities`. Returns "imported" or "reused"."""
    object_path = entry["assetPath"]
    package_root, asset_name = split_asset_path(object_path)
    rows = entry["entities"]

    # The fingerprint covers the rows themselves, so a re-stage that changes one keyvalue rebuilds
    # and a re-stage that changes nothing does not.
    fingerprint = bl.recipe_fingerprint(
        STAGE, object_path,
        {"recipeVersion": entry.get("recipeVersion"), "entities": rows},
    )
    if (not force and unreal.EditorAssetLibrary.does_asset_exist(object_path)
            and bl.asset_class_name(object_path) == ASSET_CLASS
            and bl.stored_recipe(object_path, producer='map-entities') == fingerprint):
        return "reused"

    bl.ensure_dir(package_root)
    asset = unreal.load_asset(object_path)
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.ElysiumMapEntities)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_root, unreal.ElysiumMapEntities, factory)
    if asset is None:
        raise RuntimeError("could not create %s" % object_path)

    asset.set_editor_property("map_name", entry["map"])
    asset.set_editor_property("entities", [make_entity_row(row) for row in rows])
    bl.stamp_recipe(asset, fingerprint, producer='map-entities')
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
            "entities": len(entry["entities"]),
            "outcome": outcome,
            "seconds": round(time.time() - started, 2),
        })
        log("%s: %d row(s) %s -> %s" % (entry["map"], len(entry["entities"]), outcome,
                                        entry["assetPath"]))

    report_path = os.path.join(staging_root, "import_report.json")
    with open(report_path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
    log("%d imported / %d reused / %d failed -> %s"
        % (report["imported"], report["reused"], len(report["failed"]), report_path))
    return report


def main():
    manifest_path = cmdline_arg("ImportMapEntities", "")
    if not manifest_path:
        raise SystemExit("[import-map-entities] -ImportMapEntities=<manifest.json> is required")
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
