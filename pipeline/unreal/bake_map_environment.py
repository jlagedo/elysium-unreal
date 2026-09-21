"""The environment stage of `uv run elysium bake map`: one staged environment per map, landed as a
`UElysiumMapEnvironment` data asset under `/ElysiumBaked/<map>/DA_<map>_Environment`.

Runs inside the bake's own editor session, called from `bake_map.bake_one`. The offline stage
(`importers/map_environment.py`, R4.4) already read `<map>.env`, `<map>.sky` and `<map>.spawn` and
asserted parity against them; this module only turns those values into reflected properties and
saves them.

Nothing is decided here. Every value written below is copied from the manifest row verbatim -- the
asset is a transport change and nothing else.

Until 0018 story 21-2 this was `import_map_environment.py`, phase 2 of a command of its own with
its own editor boot. The command is gone: one `bake map` yields a loadable level, so the authoring
is a stage of that bake and this module is imported rather than run.
"""
from __future__ import annotations

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402

#: The recipe stage label every fingerprint hashes under -- the lane's own name. It is also the
#: producer tag on the asset, so this lane's stamps never collide with the map bake's own.
STAGE = "map_environment"
PRODUCER = "map-environment"

#: Manifest schema this module understands.
MANIFEST_SCHEMA = "1.0.0"

#: The one class this lane authors.
ASSET_CLASS = "ElysiumMapEnvironment"

#: What an entry must carry to be executable.
REQUIRED_KEYS = ("map", "assetPath", "packageRoot", "env", "sky", "spawn", "parity")


def log(msg):
    unreal.log("[bake-map-environment] %s" % msg)


def load_manifest(path):
    return bl.load_stage_manifest(
        path, schema=MANIFEST_SCHEMA, required=REQUIRED_KEYS, lane="map-environment")


def make_vector(triple):
    return unreal.Vector(float(triple[0]), float(triple[1]), float(triple[2]))


def make_color(triple):
    return unreal.LinearColor(float(triple[0]), float(triple[1]), float(triple[2]), 1.0)


def author(entry, force=False):
    """Author or reuse one map's `UElysiumMapEnvironment`. Returns "imported" or "reused"."""
    object_path = entry["assetPath"]
    package_root, asset_name = bl.split_asset_path(object_path)
    env, sky, spawn = entry["env"], entry["sky"], entry["spawn"]

    # The fingerprint covers every value the asset carries, so a re-stage that moves the sky
    # miniature rebuilds and a re-stage that changes nothing does not.
    fingerprint = bl.recipe_fingerprint(
        STAGE, object_path,
        {"recipeVersion": entry.get("recipeVersion"), "env": env, "sky": sky, "spawn": spawn},
    )
    if (not force and unreal.EditorAssetLibrary.does_asset_exist(object_path)
            and bl.asset_class_name(object_path) == ASSET_CLASS
            and bl.stored_recipe(object_path, producer=PRODUCER) == fingerprint):
        log("%s: sky miniature=%s spawn=%s reused -> %s"
            % (entry["map"], sky["hasMiniature"], spawn["hasSpawn"], object_path))
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

    bl.stamp_recipe(asset, fingerprint, producer=PRODUCER)
    if not bl.save(object_path):
        raise RuntimeError("save failed: %s" % object_path)
    log("%s: sky miniature=%s spawn=%s imported -> %s"
        % (entry["map"], sky["hasMiniature"], spawn["hasSpawn"], object_path))
    return "imported"
