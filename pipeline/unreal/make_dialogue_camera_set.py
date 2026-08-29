"""Rebuild the project-owned dialogue camera grammar from reviewable JSON.

The package is generated under /Game/ElysiumGenerated and ignored. It contains only numeric project tuning
and enum values; no source-game path, transform, timing, or dependency is permitted in the input.
"""

import json
from pathlib import Path

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from elysium_pipeline import mounts

SOURCE = Path(unreal.Paths.project_dir()) / "pipeline" / "unreal" / "dialogue_camera_set.json"
PACKAGE = mounts.CAMERA
NAME = "DA_ElysiumDialogueCameraSet"
TARGET = "%s/%s" % (PACKAGE, NAME)


def fail(message):
    unreal.log_error("[dialogue_camera_set] %s" % message)
    raise RuntimeError(message)


def load_definition():
    data = json.loads(SOURCE.read_text(encoding="utf-8"))
    if data.get("schema") != 1 or data.get("asset") != TARGET:
        fail("unexpected schema or asset identity in %s" % SOURCE)
    text = SOURCE.read_text(encoding="utf-8").lower()
    forbidden = ("vdata/", "camerashots", "/elysiumbaked/", "/game/vtmb/")
    if any(token in text for token in forbidden):
        fail("authored camera set contains a game-derived dependency")
    return data


def get_or_create():
    if unreal.EditorAssetLibrary.does_asset_exist(TARGET):
        return unreal.load_asset(TARGET)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.ElysiumDialogueCameraSet)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        NAME, PACKAGE, unreal.ElysiumDialogueCameraSet, factory)
    if asset is None:
        fail("could not create %s" % TARGET)
    return asset


def make_profile(row):
    profile = unreal.ElysiumDialogueCameraProfile()
    profile.set_editor_property("name", row["name"])
    profile.set_editor_property(
        "kind", getattr(unreal.ElysiumDialogueShotProfile, row["kind"]))
    for source, prop in (
        ("distance_cm", "distance_cm"),
        ("lateral_cm", "lateral_cm"),
        ("height_cm", "height_cm"),
        ("field_of_view", "field_of_view"),
        ("minimum_hold_seconds", "minimum_hold_seconds"),
        ("allow_close_up", "allow_close_up"),
        ("establishing", "establishing"),
    ):
        profile.set_editor_property(prop, row[source])
    return profile


def main():
    data = load_definition()
    asset = get_or_create()
    asset.set_editor_property("profiles", [make_profile(row) for row in data["profiles"]])
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        fail("could not save %s" % TARGET)
    unreal.log("[dialogue_camera_set] wrote %s from %s" % (TARGET, SOURCE))


main()
