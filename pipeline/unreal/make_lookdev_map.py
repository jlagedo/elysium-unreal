"""SF-4.7: build the generated lookdev map -- the review set for the V2 material masters.

Runs inside a headless editor (`-run=pythonscript -script=pipeline/unreal/make_lookdev_map.py
-LookdevSet=<path> -LookdevMap=<package path>`). This is written and unit-tested now (Phase 4,
`docs/project/seam_migration.md` -> Plan -> SF-4.7); the real run -- an actual editor session
producing a saved `.umap` -- happens once the material import (SF-4.5) has landed real `MI_`
assets to look at. Nothing here depends on that landing: a review-set entry whose `MI_` does not
exist yet is placed as a labelled placeholder on the engine default material rather than failing
the run, so the map can be generated at any point in Phase 4
(`docs/architecture/phase4_mechanics.md` -> "6. SF-4.7 Lookdev map").

Layout: a grid of `GRID_COLUMNS` bays, `GRID_PITCH_CM` apart. Each bay carries a 3x-scaled
`/Engine/BasicShapes/Plane` tile at z=0 and, unless the entry's `shape` is `"plane"`, an
`/Engine/BasicShapes/Sphere` at `SPHERE_Z`; `"both"` adds a second plane tile beside the first so
the sphere does not occlude it. A `TextRenderActor` labels the bay. The rig is a neutral
`SkyLight`, one -45 degree `DirectionalLight`, three `PointLight`s, and a `PostProcessVolume`
with exposure pinned so no auto-exposure hides a material's real values, plus a `PlayerStart` so
PIE has somewhere to spawn. `WorldSettings.default_game_mode` is pinned to the engine's bare
`GameModeBase` on this map only -- the boot-plan bypass `phase4_mechanics.md` names, so PIE does
not travel off the generated map through `AElysiumGameMode::BeginPlay` -> `NotifyWorldReady` ->
`EnterFrontEnd`. No C++ change.

Command line:
  -LookdevSet=<path>   review-set JSON (default: pipeline/unreal/lookdev_set.json, tracked)
  -LookdevMap=<path>   destination package path (default: /ElysiumBaked/Lookdev/Materials)
"""
from __future__ import annotations

import json
import os
import re

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402

#: Bays per row, and the centre-to-centre spacing between them, in the generated map's own
#: (already-Unreal) centimetres.
GRID_COLUMNS = 8
GRID_PITCH_CM = 300.0
#: Sphere pivot height above the plane tile.
SPHERE_Z = 150.0
#: The plane tile's uniform scale -- `/Engine/BasicShapes/Plane` is a 100x100cm unit quad, so 3x
#: covers a bay with room either side of the sphere/label.
PLANE_SCALE = 3.0
#: Offset of a `"both"` entry's second plane tile from the first, so the sphere sitting on the
#: first tile does not hide it.
SECOND_PLANE_OFFSET_CM = GRID_PITCH_CM * 0.5

DEFAULT_SET_PATH = os.path.join(os.path.dirname(__file__), "lookdev_set.json")
DEFAULT_MAP_PATH = "/ElysiumBaked/Lookdev/Materials"

SPHERE_MESH_PATH = "/Engine/BasicShapes/Sphere.Sphere"
PLANE_MESH_PATH = "/Engine/BasicShapes/Plane.Plane"

#: Neutral grey the sky light samples. `/Engine/EngineResources/DefaultTextureCube` is also every
#: V2 master's own `EnvMap` default (seam_map_material.md -> "M_V2_Lit / M_V2_LitTranslucent"),
#: so the rig lights every entry -- including one whose instance falls back to that default --
#: with the same image the master would show it anyway. Picked over `SLS_CapturedScene` because a
#: real asset already exists on disk; a captured scene would need something in the (empty) scene
#: to capture.
NEUTRAL_SKY_CUBEMAP_PATH = "/Engine/EngineResources/DefaultTextureCube.DefaultTextureCube"

#: Three-point layout around the grid's centre, high enough to clear the spheres. Not a claim
#: about any one entry's key/fill/rim role -- just three lights so no bay is lit from one side
#: only.
POINT_LIGHT_OFFSETS_CM = (
    (600.0, 0.0, 400.0),
    (-600.0, 600.0, 400.0),
    (-600.0, -600.0, 400.0),
)

PLAYER_START_LOCATION = (-800.0, 0.0, 180.0)


def log(msg):
    unreal.log("[lookdev] %s" % msg)


def warn(msg):
    unreal.log_warning("[lookdev] %s" % msg)


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line.

    A quoted token (`"-LookdevSet=C:/path with spaces/lookdev_set.json"`) is one token: the
    launcher quotes a path argument, and a work root under a user's home may carry a space.
    """
    needle = "-%s=" % key
    for token in re.findall(r'"[^"]*"|\S+', unreal.SystemLibrary.get_command_line()):
        token = token.strip('"')
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def load_review_set(path):
    """The tracked JSON's `entries` list, or raise if the file is missing or malformed."""
    with open(path, "r", encoding="utf-8") as handle:
        document = json.load(handle)
    entries = document.get("entries")
    if not isinstance(entries, list) or not entries:
        raise ValueError("%s: no non-empty 'entries' array" % path)
    return entries


def grid_position(index):
    """(x, y) centre of the bay at this index, `GRID_COLUMNS` wide, `GRID_PITCH_CM` apart."""
    column = index % GRID_COLUMNS
    row = index // GRID_COLUMNS
    return column * GRID_PITCH_CM, row * GRID_PITCH_CM


def _spawn_mesh(actors, mesh_path, location, label):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, location)
    actor.set_actor_label(label)
    component = actor.static_mesh_component
    component.set_static_mesh(unreal.load_asset(mesh_path))
    return actor, component


def place_entry(actors, index, entry):
    """Spawn one bay's plane(s), sphere (if any) and label. Returns True iff its MI_ was found."""
    x, y = grid_position(index)
    label = entry["label"]
    material_path = entry.get("material")
    material = unreal.load_asset(material_path) if material_path else None
    placed = material is not None
    if not placed:
        warn("%s: material asset not found: %s (placed as a labelled placeholder)" % (
            entry.get("unit", label), material_path))

    def apply(component):
        if material is not None:
            component.set_material(0, material)
        # else: leave the spawned mesh on its engine default material -- an explicit,
        # visible placeholder rather than a crash or a silently blank bay.

    plane_actor, plane_component = _spawn_mesh(
        actors, PLANE_MESH_PATH, unreal.Vector(x, y, 0.0), "Plane_%02d_%s" % (index, label))
    plane_actor.set_actor_scale3d(unreal.Vector(PLANE_SCALE, PLANE_SCALE, PLANE_SCALE))
    apply(plane_component)

    shape = entry.get("shape", "sphere")
    if shape in ("sphere", "both"):
        _, sphere_component = _spawn_mesh(
            actors, SPHERE_MESH_PATH, unreal.Vector(x, y, SPHERE_Z),
            "Sphere_%02d_%s" % (index, label))
        apply(sphere_component)

    if shape == "both":
        second_actor, second_component = _spawn_mesh(
            actors, PLANE_MESH_PATH, unreal.Vector(x, y + SECOND_PLANE_OFFSET_CM, 0.0),
            "PlaneB_%02d_%s" % (index, label))
        second_actor.set_actor_scale3d(unreal.Vector(PLANE_SCALE, PLANE_SCALE, PLANE_SCALE))
        apply(second_component)

    text_actor = actors.spawn_actor_from_class(
        unreal.TextRenderActor, unreal.Vector(x, y, -20.0))
    text_actor.set_actor_label("Label_%02d" % index)
    text_component = text_actor.text_render
    text_component.set_editor_property("text", unreal.Text(label))
    text_component.set_editor_property("world_size", 24.0)

    return placed


def place_rig(actors):
    """Neutral SkyLight, one -45 degree DirectionalLight, three PointLights, a PostProcessVolume
    with exposure pinned, and a PlayerStart. Nothing here reads the review set."""
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 0.0))
    sky.set_actor_label("SkyLight")
    sky_component = sky.light_component
    sky_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky_component.set_editor_property(
        "source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky_component.set_editor_property("cubemap", unreal.load_asset(NEUTRAL_SKY_CUBEMAP_PATH))
    sky_component.set_editor_property("intensity", 1.0)

    sun = actors.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 800.0),
        unreal.Rotator(-45.0, 0.0, 0.0))
    sun.set_actor_label("DirectionalLight")
    sun_component = sun.light_component
    sun_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun_component.set_editor_property("intensity", 3.0)

    for point_index, (px, py, pz) in enumerate(POINT_LIGHT_OFFSETS_CM):
        point = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(px, py, pz))
        point.set_actor_label("PointLight_%d" % point_index)
        point.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)

    ppv = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0.0, 0.0, 0.0))
    ppv.set_actor_label("PostProcessVolume")
    ppv.set_editor_property("unbound", True)
    settings = ppv.settings
    settings.set_editor_property("bOverride_AutoExposureMethod", True)
    settings.set_editor_property(
        "auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("bOverride_AutoExposureMinBrightness", True)
    settings.set_editor_property("auto_exposure_min_brightness", 1.0)
    settings.set_editor_property("bOverride_AutoExposureMaxBrightness", True)
    settings.set_editor_property("auto_exposure_max_brightness", 1.0)
    ppv.set_editor_property("settings", settings)

    start = actors.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(*PLAYER_START_LOCATION))
    start.set_actor_label("PlayerStart")


def build(set_path, map_path):
    entries = load_review_set(set_path)

    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if not world:
        raise RuntimeError("new_blank_map returned null")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    placed = missing = 0
    for index, entry in enumerate(entries):
        if place_entry(actors, index, entry):
            placed += 1
        else:
            missing += 1

    place_rig(actors)

    # The boot-plan bypass (phase4_mechanics.md -> "6."): pin this generated map's own game mode
    # to the engine's bare GameModeBase so PIE stays on it instead of travelling off through
    # AElysiumGameMode::BeginPlay -> NotifyWorldReady -> EnterFrontEnd. No C++ change; the map's
    # own WorldSettings carries the override.
    world.get_world_settings().set_editor_property("default_game_mode", unreal.GameModeBase)

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
        raise RuntimeError("save_map failed: %s" % map_path)

    log("%s: %d entr%s placed, %d missing (%s)" % (
        map_path, placed, "y" if placed == 1 else "ies", missing, set_path))
    return placed, missing


def main():
    set_path = cmdline_arg("LookdevSet", DEFAULT_SET_PATH)
    map_path = cmdline_arg("LookdevMap", DEFAULT_MAP_PATH)
    try:
        build(set_path, map_path)
    except (OSError, ValueError, RuntimeError) as exc:
        unreal.log_error("[lookdev] refused: %s" % exc)
        raise SystemExit(1)


main()
