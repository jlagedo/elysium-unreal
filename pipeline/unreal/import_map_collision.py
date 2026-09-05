"""Phase 2 of `uv run elysium import map-collision`: land one staged collision payload per map as a
`UElysiumMapCollisionPayload` asset under `/ElysiumBaked/<map>/DA_<map>_Collision`.

Runs inside a headless editor (`-run=pythonscript -script=pipeline/unreal/import_map_collision.py
-ImportMapCollision=<manifest.json>`). The offline stage (`importers/map_collision.py`, R4.2) already
read `<map>.hulls`, `<map>.dispcol` and the brush-entity `hulls` of `<map>.ents`, applied the one
transform the runtime applies (the 3D-skybox scale) and asserted parity against those files; this
script only turns those numbers into cooked `UBodySetup`s and saves them
(`docs/architecture/seam_map_map.md` -> "Import").

Nothing is decided here. Every number written below is copied from the manifest verbatim, and every
body-setup flag is set by the asset's own C++ authoring functions rather than by this script, so the
recipe has one owner.

  * per map, compare the manifest recipe against the stamp the asset carries
    (`bake_lib.RECIPE_TAG`) and touch only what is new, changed or forced;
  * cook every authored setup at import time, so a payload that cannot cook fails here and not at
    map load;
  * write `import_report.json` beside the manifest and exit non-zero if any map failed.

Command line:
  -ImportMapCollision=<path>  the manifest (required)
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
STAGE = "map_collision"

#: Manifest schema this script understands.
MANIFEST_SCHEMA = "1.0.0"

#: The one class this lane authors.
ASSET_CLASS = "ElysiumMapCollisionPayload"


def log(msg):
    unreal.log("[import-map-collision] %s" % msg)


def fail(msg):
    unreal.log_error("[import-map-collision] %s" % msg)


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
        for key in ("map", "assetPath", "packageRoot", "worldHulls", "parity"):
            if key not in entry:
                raise ManifestError("maps[%d] lacks %r" % (index, key))
        if not entry["assetPath"].startswith(mount + "/"):
            raise ManifestError("maps[%d] %s is outside %s" % (index, entry["assetPath"], mount))
        # The stage refuses to write an entry whose parity failed, so an unchecked or unequal
        # payload reaching this process is a manifest that must not be executed.
        parity = entry["parity"]
        if not parity.get("checked") or not parity.get("equal"):
            raise ManifestError("maps[%d] %s carries no passing parity verdict: %r"
                                % (index, entry["map"], parity))
    return manifest


def split_asset_path(asset_path):
    """`/Root/dir/DA_name` -> (`/Root/dir`, `DA_name`)."""
    package, _, name = asset_path.rpartition("/")
    return package, name


def make_hull(flat):
    """One convex volume: the sidecar's flat `x y z x y z ...` row, whole triples only."""
    hull = unreal.ElysiumCollisionHull()
    hull.set_editor_property(
        "vertices",
        [unreal.Vector(flat[i], flat[i + 1], flat[i + 2]) for i in range(0, len(flat) - 2, 3)],
    )
    return hull


def make_hulls(rows):
    return [make_hull(row) for row in rows]


def author_map(entry, force=False):
    """Author or reuse one map's `UElysiumMapCollisionPayload`. Returns "imported" or "reused"."""
    object_path = entry["assetPath"]
    package_root, asset_name = split_asset_path(object_path)

    # The fingerprint covers every number the asset carries, so a re-stage that moves one vertex
    # rebuilds and a re-stage that changes nothing does not.
    fingerprint = bl.recipe_fingerprint(
        STAGE, object_path,
        {
            "recipeVersion": entry.get("recipeVersion"),
            "skyScale": entry.get("skyScale"),
            "worldHulls": entry["worldHulls"],
            "displacementVertices": entry.get("displacementVertices", []),
            "displacementIndices": entry.get("displacementIndices", []),
            "brushBodies": entry.get("brushBodies", []),
        },
    )
    if (not force and unreal.EditorAssetLibrary.does_asset_exist(object_path)
            and bl.asset_class_name(object_path) == ASSET_CLASS
            and bl.stored_recipe(object_path, producer='map-collision') == fingerprint):
        return "reused"

    bl.ensure_dir(package_root)
    asset = unreal.load_asset(object_path)
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.ElysiumMapCollisionPayload)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_root, unreal.ElysiumMapCollisionPayload, factory)
    if asset is None:
        raise RuntimeError("could not create %s" % object_path)

    asset.set_editor_property("map_name", entry["map"])
    # Re-authoring an existing asset drops what was there first: the body setups are subobjects of
    # this package, and leaving the previous set behind would carry it into the save.
    asset.reset_authoring()
    asset.author_world_hulls(make_hulls(entry["worldHulls"]))

    flat = entry.get("displacementVertices", [])
    vertices = [unreal.Vector(flat[i], flat[i + 1], flat[i + 2])
                for i in range(0, len(flat) - 2, 3)]
    asset.author_displacement(vertices, [int(i) for i in entry.get("displacementIndices", [])])

    for row in entry.get("brushBodies", []):
        asset.author_brush_body(int(row["entityIndex"]), make_hulls(row["hulls"]))

    # Cook now, in the import, so the DDC entry exists before any map load and a payload that
    # cannot cook is this run's failure rather than a silent fallback at runtime.
    failure = asset.cook_authored()
    if failure:
        raise RuntimeError("collision cook failed for %s (%s)" % (object_path, failure))

    bl.stamp_recipe(asset, fingerprint, producer='map-collision')
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
        stats = entry.get("stats", {})
        report["maps"].append({
            "map": entry["map"],
            "assetPath": entry["assetPath"],
            "worldHulls": stats.get("worldHulls", len(entry["worldHulls"])),
            "displacementTriangles": stats.get("displacementTriangles", 0),
            "brushBodies": stats.get("brushBodies", len(entry.get("brushBodies", []))),
            "outcome": outcome,
            "seconds": round(time.time() - started, 2),
        })
        log("%s: %d world hull(s), %d disp tri(s), %d brush body(ies) %s -> %s"
            % (entry["map"], stats.get("worldHulls", 0), stats.get("displacementTriangles", 0),
               stats.get("brushBodies", 0), outcome, entry["assetPath"]))

    report_path = os.path.join(staging_root, "import_report.json")
    with open(report_path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
    log("%d imported / %d reused / %d failed -> %s"
        % (report["imported"], report["reused"], len(report["failed"]), report_path))
    return report


def main():
    manifest_path = cmdline_arg("ImportMapCollision", "")
    if not manifest_path:
        raise SystemExit("[import-map-collision] -ImportMapCollision=<manifest.json> is required")
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
