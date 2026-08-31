"""SF-4.7: build the generated lookdev map -- the review set for the V2 material masters.

Runs inside a headless editor (`-run=pythonscript -script=pipeline/unreal/make_lookdev_map.py
-LookdevSet=<path> -LookdevMap=<package path>`). This is written and unit-tested now (Phase 4,
`docs/project/seam_migration.md` -> Plan -> SF-4.7); the real run -- an actual editor session
producing a saved `.umap` -- happens once the material import (SF-4.5) has landed real `MI_`
assets to look at. Nothing here depends on that landing: a review-set entry whose `MI_` does not
exist yet is placed on a loud, unmistakable placeholder material rather than failing the run, so
the map can be generated at any point in Phase 4 (`docs/architecture/phase4_mechanics.md` ->
"6. SF-4.7 Lookdev map") -- but the run itself now fails (`SystemExit(1)`) once any entry is
missing, unless `-LookdevAllowMissing=1` says that is expected right now. A silent placeholder
that never fails the run is how a missing asset goes unnoticed; a loud one that does fail is not.

Layout: a grid of `GRID_COLUMNS` bays, `GRID_PITCH_CM` apart. Each bay carries a 3x-scaled
`/Engine/BasicShapes/Plane` tile at z=0 and, unless the entry's `shape` is `"plane"`, an
`/Engine/BasicShapes/Sphere` sitting on the tile. A `TextRenderActor` labels the bay, in front of
it and facing the aisle, not stacked underneath. The rig is a neutral `SkyLight`, one -45 degree
`DirectionalLight`, three `PointLight`s centred on the grid and reaching across all of it, and a
`PostProcessVolume` with exposure pinned so no auto-exposure -- and no physical-camera exposure --
hides a material's real values, plus a `PlayerStart` so PIE has somewhere to spawn.
`WorldSettings.default_game_mode` is pinned to the engine's bare `GameModeBase` on this map only
-- the boot-plan bypass `phase4_mechanics.md` names, so PIE does not travel off the generated map
through `AElysiumGameMode::BeginPlay` -> `NotifyWorldReady` -> `EnterFrontEnd`. No C++ change.

Command line:
  -LookdevSet=<path>          review-set JSON (default: pipeline/unreal/lookdev_set.json, tracked)
  -LookdevMap=<path>          destination package path (default: /ElysiumBaked/Lookdev/Materials)
  -LookdevReport=<path>       OS path for `lookdev_report.json` (default: a temp-dir path; the
                               real launcher, `elysium_pipeline.unreal.make_lookdev_map`, always
                               passes one under `$ELYSIUM_WORK_ROOT/reports/lookdev/`)
  -LookdevAllowMissing=1      do not fail the run when a review-set entry's `MI_` is missing

`pipeline/CLAUDE.md` documents the convention this script follows: `main()` runs at module scope,
with no `if __name__ == "__main__":` guard, exactly like `make_player_anim_bp.py` -- the commandlet
invokes a script with `-script=`, not a package entry point, and re-running the whole build on
import is the intended way this is invoked. Importing the module for its helpers (as the tests do)
re-runs it as a side effect; that is accepted, not a bug.
"""
from __future__ import annotations

import json
import math
import os
import re
import tempfile

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402

#: Bays per row, and the centre-to-centre spacing between them, in the generated map's own
#: (already-Unreal) centimetres.
GRID_COLUMNS = 8
GRID_PITCH_CM = 300.0
#: Sphere pivot height above the plane tile. `/Engine/BasicShapes/Sphere` is a unit sphere of
#: radius 50cm, unscaled here (only the plane tiles are scaled), and the plane sits at z=0, so a
#: pivot at z=50 rests the sphere's bottom exactly on the tile. (Previously 150 -- floating the
#: sphere a metre above the floor for no reason a review is supposed to catch.)
SPHERE_Z = 50.0
#: The plane tile's uniform scale -- `/Engine/BasicShapes/Plane` is a 100x100cm unit quad, so 3x
#: covers a bay with room either side of the sphere/label.
PLANE_SCALE = 3.0

#: A label's text is wrapped so no line exceeds this many characters, keeping it inside the
#: 300cm (`GRID_PITCH_CM`) bay pitch at the `LABEL_WORLD_SIZE` below rather than overrunning into
#: the neighbouring bay.
LABEL_MAX_CHARS_PER_LINE = 20
#: `TextRenderComponent` draws in its own local YZ plane at local X=0 (`TextRenderComponent.cpp`,
#: `CalcBounds`); at identity rotation that plane's normal is world +X -- the same axis the bays
#: advance along. A label planted at a bay's own (x, y) and left unrotated is therefore edge-on to
#: anyone walking the row (+X) from the `PlayerStart` at -X: every bay's label lines up behind the
#: next one, "stacked in depth" rather than read in turn. Yawing the actor -90 degrees turns that
#: local-X normal to face world -Y instead, so the label reads from the aisle side of the row (Y
#: at or below the bay's own) rather than from directly down the corridor.
LABEL_YAW_DEGREES = -90.0
#: Offset toward -Y (the aisle) from the bay centre, and a small +Z lift off the floor -- the
#: previous placement (z=-20 with the tile at z=0) put the label a full 20cm *under* the plane,
#: invisible rather than merely mis-aligned.
LABEL_FRONT_OFFSET_CM = 130.0
LABEL_Z_CM = 20.0
LABEL_WORLD_SIZE = 20.0

DEFAULT_SET_PATH = os.path.join(os.path.dirname(__file__), "lookdev_set.json")
DEFAULT_MAP_PATH = "/ElysiumBaked/Lookdev/Materials"
#: Only used when nobody passes `-LookdevReport=`; the real launcher
#: (`elysium_pipeline.unreal.make_lookdev_map`) always does, so this is a fallback for a bare
#: manual run, not a location anything durable should read from.
DEFAULT_REPORT_PATH = os.path.join(tempfile.gettempdir(), "elysium_lookdev_report.json")

SPHERE_MESH_PATH = "/Engine/BasicShapes/Sphere.Sphere"
PLANE_MESH_PATH = "/Engine/BasicShapes/Plane.Plane"

#: Neutral grey the sky light samples. `/Engine/EngineResources/DefaultTextureCube` is also every
#: V2 master's own `EnvMap` default (seam_map_material.md -> "M_V2_Lit / M_V2_LitTranslucent"),
#: so the rig lights every entry -- including one whose instance falls back to that default --
#: with the same image the master would show it anyway. Picked over `SLS_CapturedScene` because a
#: real asset already exists on disk; a captured scene would need something in the (empty) scene
#: to capture.
NEUTRAL_SKY_CUBEMAP_PATH = "/Engine/EngineResources/DefaultTextureCube.DefaultTextureCube"

#: A missing `MI_` gets this instead of the spawned mesh's engine-default material. Loud on
#: purpose -- a bright per-vertex-colour checker nothing in the V2 masters produces -- so a gap in
#: the corpus reads as a gap on sight, not as "a slightly plain-looking bay". An emissive material
#: authored just for this would do the same job; this one already ships with the engine.
PLACEHOLDER_MATERIAL_PATH = "/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"
MISSING_SUFFIX = " [MISSING]"

#: Three-point layout around the grid's centroid (see `grid_centroid`), high enough to clear the
#: spheres. Not a claim about any one entry's key/fill/rim role -- just three lights so no bay is
#: lit from one side only.
POINT_LIGHT_OFFSETS_CM = (
    (600.0, 0.0, 400.0),
    (-600.0, 600.0, 400.0),
    (-600.0, -600.0, 400.0),
)
#: `LocalLightComponent`'s own default (`LightComponent.cpp`) is 1000cm, which does not reach a
#: light positioned ~600-1000cm off one corner of an eight-column, multi-row grid to the far
#: corner. Set explicitly so every bay gets comparable light regardless of where it sits in the
#: grid.
POINT_LIGHT_ATTENUATION_RADIUS_CM = 3000.0

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


def flag(value):
    return value.strip() not in ("", "0")


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


def grid_centroid(entry_count):
    """(x, y) centre of the whole occupied grid, for placing the point-light rig over it rather
    than over the world origin -- which a bay only sits on by coincidence (index 0)."""
    if entry_count <= 0:
        return 0.0, 0.0
    columns = min(GRID_COLUMNS, entry_count)
    rows = math.ceil(entry_count / GRID_COLUMNS)
    return (columns - 1) * GRID_PITCH_CM / 2.0, (rows - 1) * GRID_PITCH_CM / 2.0


def wrap_label(text):
    """`text` broken into lines of at most `LABEL_MAX_CHARS_PER_LINE` characters, greedily
    packing whole words. A single word longer than the limit is kept whole rather than split."""
    words = text.split(" ")
    lines = []
    current = ""
    for word in words:
        candidate = ("%s %s" % (current, word)).strip()
        if not current or len(candidate) <= LABEL_MAX_CHARS_PER_LINE:
            current = candidate
        else:
            lines.append(current)
            current = word
    if current:
        lines.append(current)
    return "\n".join(lines)


def _spawn_mesh(actors, mesh_path, location, label):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, location)
    actor.set_actor_label(label)
    component = actor.static_mesh_component
    component.set_static_mesh(unreal.load_asset(mesh_path))
    return actor, component


def place_entry(actors, index, entry):
    """Spawn one bay's plane, sphere (if any) and label. Returns True iff its MI_ was found."""
    x, y = grid_position(index)
    label = entry["label"]
    material_path = entry.get("material")
    material = unreal.load_asset(material_path) if material_path else None
    placed = material is not None
    if not placed:
        warn("%s: material asset not found: %s (placed on a loud placeholder material)" % (
            entry.get("unit", label), material_path))

    def apply(component):
        if material is not None:
            component.set_material(0, material)
        else:
            # A visible, unmistakable placeholder -- not the spawned mesh's engine-default
            # material, which looks close enough to a plain V2 baseline to go unnoticed.
            component.set_material(0, unreal.load_asset(PLACEHOLDER_MATERIAL_PATH))

    plane_actor, plane_component = _spawn_mesh(
        actors, PLANE_MESH_PATH, unreal.Vector(x, y, 0.0), "Plane_%02d_%s" % (index, label))
    plane_actor.set_actor_scale3d(unreal.Vector(PLANE_SCALE, PLANE_SCALE, PLANE_SCALE))
    apply(plane_component)

    shape = entry.get("shape", "sphere")
    if shape != "plane":
        _, sphere_component = _spawn_mesh(
            actors, SPHERE_MESH_PATH, unreal.Vector(x, y, SPHERE_Z),
            "Sphere_%02d_%s" % (index, label))
        apply(sphere_component)

    text_actor = actors.spawn_actor_from_class(
        unreal.TextRenderActor, unreal.Vector(x, y - LABEL_FRONT_OFFSET_CM, LABEL_Z_CM),
        unreal.Rotator(0.0, LABEL_YAW_DEGREES, 0.0))
    text_actor.set_actor_label("Label_%02d" % index)
    text_component = text_actor.text_render
    display_label = label if placed else label + MISSING_SUFFIX
    text_component.set_editor_property("text", unreal.Text(wrap_label(display_label)))
    text_component.set_editor_property("world_size", LABEL_WORLD_SIZE)
    text_component.set_editor_property(
        "horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)

    return placed


def place_rig(actors, centroid=(0.0, 0.0)):
    """Neutral SkyLight, one -45 degree DirectionalLight, three PointLights centred on
    `centroid`, a PostProcessVolume with exposure pinned, and a PlayerStart. Nothing here reads
    the review set except, via `centroid`, how many bays it has."""
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

    cx, cy = centroid
    for point_index, (px, py, pz) in enumerate(POINT_LIGHT_OFFSETS_CM):
        point = actors.spawn_actor_from_class(
            unreal.PointLight, unreal.Vector(cx + px, cy + py, pz))
        point.set_actor_label("PointLight_%d" % point_index)
        point_component = point.light_component
        # Mobility is set on the freshly spawned component, before anything (a lightmap bake, a
        # static-lighting-dependent default) could read the spawn-time Static default -- this map
        # is never baked, so there is nothing downstream that would need a re-register/dirty call
        # to pick the change up.
        point_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        point_component.set_editor_property(
            "attenuation_radius", POINT_LIGHT_ATTENUATION_RADIUS_CM)

    ppv = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0.0, 0.0, 0.0))
    ppv.set_actor_label("PostProcessVolume")
    ppv.set_editor_property("unbound", True)
    settings = ppv.settings
    settings.set_editor_property("bOverride_AutoExposureMethod", True)
    settings.set_editor_property(
        "auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    # AEM_Manual sets MinWhitePointLuminance == MaxWhitePointLuminance ==
    # CalculateManualAutoExposure() (PostProcessEyeAdaptation.cpp:~520-529), which by default
    # derives that white point from this same settings block's "physical camera" fields
    # (aperture/shutter/ISO) -- an EV100 that renders the map black unless the project's
    # r.DefaultFeature.AutoExposure=False (Config/DefaultEngine.ini:57) happens to be clearing
    # eye adaptation globally. Pinning AutoExposureApplyPhysicalCameraExposure=False makes
    # CalculateManualAutoExposure's PhysicalCameraEV100 exactly 0 regardless of the physical
    # camera fields (same file, ~line 527), giving a fixed, camera-independent white point --
    # correct on its own, not because some other cvar happens to help it. AutoExposureBias is
    # pinned to 0 too so no residual compensation stacks on top (same file, ~line 451: it is a
    # separate multiplier applied after the white point, default 0, pinned here to be sure).
    # AEM_Basic with Min==Max==1.0 (the review's other option) would work as well, but only
    # AutoExposureMinBrightness/MaxBrightness -- not Min/Max in general -- are read by the
    # Basic/Histogram branch; they are *not* read in Manual mode (same file, the AEM_Manual
    # branch calls CalculateManualAutoExposure and never touches those two fields), so a
    # previous version of this rig that pinned them under AEM_Manual was pinning dead settings.
    settings.set_editor_property("bOverride_AutoExposureApplyPhysicalCameraExposure", True)
    settings.set_editor_property("AutoExposureApplyPhysicalCameraExposure", False)
    settings.set_editor_property("bOverride_AutoExposureBias", True)
    settings.set_editor_property("AutoExposureBias", 0.0)
    ppv.set_editor_property("settings", settings)

    start = actors.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(*PLAYER_START_LOCATION))
    start.set_actor_label("PlayerStart")


def _write_report(report_path, map_path, placed, missing_details, actor_labels):
    """`lookdev_report.json`: `{map, placed, missing: [...], actors: [...]}`. Read back by
    `elysium_pipeline.unreal.make_lookdev_map` (and, through it, the CLI) so the owner sees the
    same placed/missing counts this run's log already carries, without scraping the log."""
    payload = {
        "map": map_path,
        "placed": placed,
        "missing": missing_details,
        "actors": actor_labels,
    }
    directory = os.path.dirname(report_path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2)
        handle.write("\n")


def build(set_path, map_path, report_path=None):
    entries = load_review_set(set_path)

    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if not world:
        raise RuntimeError("new_blank_map returned null")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    placed = 0
    missing_details = []
    for index, entry in enumerate(entries):
        if place_entry(actors, index, entry):
            placed += 1
        else:
            missing_details.append({
                "label": entry.get("label"),
                "unit": entry.get("unit"),
                "material": entry.get("material"),
            })

    place_rig(actors, grid_centroid(len(entries)))

    # The boot-plan bypass (phase4_mechanics.md -> "6."): pin this generated map's own game mode
    # to the engine's bare GameModeBase so PIE stays on it instead of travelling off through
    # AElysiumGameMode::BeginPlay -> NotifyWorldReady -> EnterFrontEnd. No C++ change; the map's
    # own WorldSettings carries the override.
    world.get_world_settings().set_editor_property("default_game_mode", unreal.GameModeBase)

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
        raise RuntimeError("save_map failed: %s" % map_path)

    actor_labels = [actor.get_actor_label() for actor in actors.get_all_level_actors()]
    _write_report(report_path or DEFAULT_REPORT_PATH, map_path, placed, missing_details,
                  actor_labels)

    missing = len(missing_details)
    log("%s: %d entr%s placed, %d missing (%s)" % (
        map_path, placed, "y" if placed == 1 else "ies", missing, set_path))
    return placed, missing


def main():
    set_path = cmdline_arg("LookdevSet", DEFAULT_SET_PATH)
    map_path = cmdline_arg("LookdevMap", DEFAULT_MAP_PATH)
    report_path = cmdline_arg("LookdevReport", DEFAULT_REPORT_PATH)
    allow_missing = flag(cmdline_arg("LookdevAllowMissing", "0"))
    try:
        placed, missing = build(set_path, map_path, report_path)
    except (OSError, ValueError, RuntimeError) as exc:
        unreal.log_error("[lookdev] refused: %s" % exc)
        raise SystemExit(1)
    if missing and not allow_missing:
        unreal.log_error(
            "[lookdev] %d of %d entries have no real MI_ asset yet; rerun with "
            "-LookdevAllowMissing=1 to accept placeholders, or import the corpus first "
            "(see %s)" % (missing, placed + missing, report_path))
        raise SystemExit(1)


main()
