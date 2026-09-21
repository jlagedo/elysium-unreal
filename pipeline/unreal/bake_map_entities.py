"""The entity-table stage of `uv run elysium bake map`: one staged entity table per map, landed as
a `UElysiumMapEntities` data asset under `/ElysiumBaked/<map>/DA_<map>_Entities`.

Runs inside the bake's own editor session, called from `bake_map.bake_one`. The offline stage
(`importers/map_entities.py`, R4.1) already ran the R3.2 producer's entity join and asserted
def-count and per-index parity against the `<map>.ents` document the asset replaces; this module
only turns those rows into reflected structs and saves them.

Nothing is decided here. Every value written below is copied from the manifest row verbatim -- the
asset is a transport change and nothing else, so a transformation in this file would be a
divergence from the document it reproduces.

Until 0018 story 21-2 this was `import_map_entities.py`, phase 2 of a command of its own with its
own editor boot. The command is gone: one `bake map` yields a loadable level, so the authoring is a
stage of that bake and this module is imported rather than run.
"""
from __future__ import annotations

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402

#: The recipe stage label every fingerprint hashes under -- the lane's own name. It is also the
#: producer tag on the asset, so this lane's stamps never collide with the map bake's own.
STAGE = "map_entities"
PRODUCER = "map-entities"

#: Manifest schema this module understands.
MANIFEST_SCHEMA = "1.0.0"

#: The one class this lane authors.
ASSET_CLASS = "ElysiumMapEntities"

#: What an entry must carry to be executable. `parity` left the entry in 0018 story 21-4: the
#: stage's comparison against `<map>.ents` had become the producer's join against itself, and the
#: bake writes that file itself moments before staging now.
REQUIRED_KEYS = ("map", "assetPath", "packageRoot", "entities")


def log(msg):
    unreal.log("[bake-map-entities] %s" % msg)


def load_manifest(path):
    return bl.load_stage_manifest(
        path, schema=MANIFEST_SCHEMA, required=REQUIRED_KEYS, lane="map-entities")


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


def author(entry, force=False):
    """Author or reuse one map's `UElysiumMapEntities`. Returns "imported" or "reused"."""
    object_path = entry["assetPath"]
    package_root, asset_name = bl.split_asset_path(object_path)
    rows = entry["entities"]

    # The fingerprint covers the rows themselves, so a re-stage that changes one keyvalue rebuilds
    # and a re-stage that changes nothing does not.
    fingerprint = bl.recipe_fingerprint(
        STAGE, object_path,
        {"recipeVersion": entry.get("recipeVersion"), "entities": rows},
    )
    if (not force and unreal.EditorAssetLibrary.does_asset_exist(object_path)
            and bl.asset_class_name(object_path) == ASSET_CLASS
            and bl.stored_recipe(object_path, producer=PRODUCER) == fingerprint):
        log("%s: %d row(s) reused -> %s" % (entry["map"], len(rows), object_path))
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
    bl.stamp_recipe(asset, fingerprint, producer=PRODUCER)
    if not bl.save(object_path):
        raise RuntimeError("save failed: %s" % object_path)
    log("%s: %d row(s) imported -> %s" % (entry["map"], len(rows), object_path))
    return "imported"
