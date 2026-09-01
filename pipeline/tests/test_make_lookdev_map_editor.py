"""`pipeline/unreal/make_lookdev_map.py`, SF-4.7's lookdev map generator, exercised against a
faked editor.

The script is an editor entry point, so the module is loaded with a fake `unreal` the way
`test_import_textures_editor.py` fakes one for the texture import script: one registry of
"assets" `load_asset` resolves against, and a `spawn_actor_from_class` that records every actor
the script asks for so placement, labelling and the rig can be asserted end to end.
"""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]


# --- the fake editor ------------------------------------------------------------------------


class FakeComponent:
    def __init__(self):
        self.props = {}

    def set_editor_property(self, name, value):
        self.props[name] = value

    def set_static_mesh(self, mesh):
        self.props["static_mesh"] = mesh

    def set_material(self, index, material):
        self.props.setdefault("materials", {})[index] = material

    def set_mobility(self, mobility):
        self.props["mobility"] = mobility


class FakeActor:
    def __init__(self, class_name, location, rotation=None):
        self.class_name = class_name
        self.location = location
        self.rotation = rotation
        self.label = None
        self.scale = None
        self.props = {}
        self.static_mesh_component = FakeComponent()
        self.light_component = FakeComponent()
        self.text_render = FakeComponent()
        self.settings = FakeComponent()

    def set_actor_label(self, label):
        self.label = label

    def get_actor_label(self):
        return self.label

    def set_actor_scale3d(self, scale):
        self.scale = scale

    def set_editor_property(self, name, value):
        self.props[name] = value


class FakeWorld:
    def __init__(self):
        self.world_settings = FakeComponent()

    def get_world_settings(self):
        return self.world_settings


class FakeActorSubsystem:
    def __init__(self, editor):
        self.editor = editor

    def spawn_actor_from_class(self, cls, location, rotation=None):
        actor = FakeActor(getattr(cls, "__name__", str(cls)), location, rotation)
        self.editor.actors.append(actor)
        return actor

    def get_all_level_actors(self):
        return list(self.editor.actors)


class FakeAsset:
    def __init__(self, path):
        self.path = path


class FakeEditor:
    """One registry of "assets" plus every actor and save call the script makes."""

    def __init__(self):
        self.assets = {}
        self.actors = []
        self.saved = []
        self.worlds = []
        self.logs, self.warnings, self.errors = [], [], []
        self.command_line = ""
        self.actor_subsystem = FakeActorSubsystem(self)
        self.new_blank_map_raises = False
        self.save_map_returns = True

    def new_blank_map(self, transient):
        if self.new_blank_map_raises:
            return None
        world = FakeWorld()
        self.worlds.append(world)
        return world

    def save_map(self, world, path):
        if self.save_map_returns:
            self.saved.append((world, path))
        return self.save_map_returns

    def load_asset(self, path):
        return self.assets.get(path)


def _fake_unreal(editor):
    return SimpleNamespace(
        log=editor.logs.append,
        log_warning=editor.warnings.append,
        log_error=editor.errors.append,
        SystemLibrary=SimpleNamespace(get_command_line=lambda: editor.command_line),
        EditorLoadingAndSavingUtils=SimpleNamespace(
            new_blank_map=editor.new_blank_map, save_map=editor.save_map),
        get_editor_subsystem=lambda cls: editor.actor_subsystem,
        load_asset=editor.load_asset,
        Vector=lambda x, y, z: (x, y, z),
        Rotator=lambda pitch, yaw, roll: (pitch, yaw, roll),
        Text=lambda value: value,
        StaticMeshActor=type("StaticMeshActor", (), {}),
        TextRenderActor=type("TextRenderActor", (), {}),
        SkyLight=type("SkyLight", (), {}),
        DirectionalLight=type("DirectionalLight", (), {}),
        PointLight=type("PointLight", (), {}),
        PostProcessVolume=type("PostProcessVolume", (), {}),
        PlayerStart=type("PlayerStart", (), {}),
        EditorActorSubsystem=type("EditorActorSubsystem", (), {}),
        ComponentMobility=SimpleNamespace(MOVABLE="MOVABLE"),
        SkyLightSourceType=SimpleNamespace(SLS_SPECIFIED_CUBEMAP="SLS_SPECIFIED_CUBEMAP"),
        AutoExposureMethod=SimpleNamespace(AEM_MANUAL="AEM_MANUAL"),
        HorizTextAligment=SimpleNamespace(EHTA_CENTER="EHTA_CENTER"),
        GameModeBase=type("GameModeBase", (), {}),
    )


def _load(editor):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_make_lookdev_map", REPO / "pipeline/unreal/make_lookdev_map.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    fake = _fake_unreal(editor)
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        with pytest.raises(SystemExit):
            # main() runs once at import, against the tracked set; every entry in it is missing
            # against this empty fake registry, so the run now fails loudly (finding D) instead
            # of quietly succeeding -- expected here, not this test's scenario.
            spec.loader.exec_module(module)
    module.unreal = fake
    # The engine debug material the placeholder path names always exists on a real machine; the
    # fake registry starts empty, so every test that exercises a placeholder needs it registered.
    editor.assets.setdefault(
        module.PLACEHOLDER_MATERIAL_PATH, FakeAsset(module.PLACEHOLDER_MATERIAL_PATH))
    # Start each test clean.
    editor.actors.clear()
    editor.saved.clear()
    editor.worlds.clear()
    editor.logs.clear()
    editor.warnings.clear()
    editor.errors.clear()
    return module


def _write_set(tmp_path, entries, name="lookdev_set.json"):
    path = tmp_path / name
    path.write_text(json.dumps({"schemaVersion": "1.0.0", "entries": entries}), encoding="utf-8")
    return path


def _entry(label, unit, material, shape):
    return {"label": label, "unit": unit, "material": material, "shape": shape}


def _prop_entry(label, unit, mesh):
    return {"label": label, "unit": unit, "mesh": mesh}


def _write_props_set(tmp_path, entries, name="lookdev_props_set.json"):
    path = tmp_path / name
    path.write_text(json.dumps({"schemaVersion": "1.0.0", "entries": entries}), encoding="utf-8")
    return path


# --- the default tracked set loads and runs at import, and now fails loudly on missing ---------


def test_module_import_runs_the_default_tracked_set_and_fails_loudly():
    editor = FakeEditor()
    # Before _load()'s own cleanup, the import-time `main()` call already built the tracked,
    # committed review set (no -LookdevSet= override) against an editor with no assets at all --
    # every entry lands on the loud placeholder, the map still saves, and the run then exits
    # non-zero because nothing passed -LookdevAllowMissing=1. Check that directly.
    spec = importlib.util.spec_from_file_location(
        "elysium_test_make_lookdev_map_default", REPO / "pipeline/unreal/make_lookdev_map.py")
    module = importlib.util.module_from_spec(spec)
    fake = _fake_unreal(editor)
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        with pytest.raises(SystemExit) as failed:
            spec.loader.exec_module(module)
    assert failed.value.code == 1
    tracked = json.loads((REPO / "pipeline/unreal/lookdev_set.json").read_text(encoding="utf-8"))
    tracked_props = json.loads(
        (REPO / "pipeline/unreal/lookdev_props_set.json").read_text(encoding="utf-8"))
    entry_count = len(tracked["entries"]) + len(tracked_props["entries"])
    assert len(editor.warnings) == entry_count      # every entry missing against an empty registry
    assert editor.saved == [(editor.worlds[0], module.DEFAULT_MAP_PATH)]


# --- grid positions and centroid are deterministic --------------------------------------------


def test_grid_positions_are_deterministic():
    editor = FakeEditor()
    module = _load(editor)
    assert module.grid_position(0) == (0.0, 0.0)
    assert module.grid_position(1) == (300.0, 0.0)
    assert module.grid_position(7) == (2100.0, 0.0)
    assert module.grid_position(8) == (0.0, 300.0)         # wraps after GRID_COLUMNS
    assert module.grid_position(9) == (300.0, 300.0)


def test_grid_centroid_covers_a_partial_last_row():
    editor = FakeEditor()
    module = _load(editor)
    assert module.grid_centroid(0) == (0.0, 0.0)
    assert module.grid_centroid(1) == (0.0, 0.0)
    assert module.grid_centroid(8) == (1050.0, 0.0)
    assert module.grid_centroid(22) == (1050.0, 300.0)      # 3 rows, last one partial


# --- label wrapping -----------------------------------------------------------------------------


def test_wrap_label_leaves_short_text_on_one_line():
    editor = FakeEditor()
    module = _load(editor)
    assert module.wrap_label("Chrome fixture") == "Chrome fixture"


def test_wrap_label_keeps_every_line_within_the_pitch_budget():
    editor = FakeEditor()
    module = _load(editor)
    text = "Glass (translucent, fixed cube)"
    wrapped = module.wrap_label(text)
    assert all(len(line) <= module.LABEL_MAX_CHARS_PER_LINE for line in wrapped.split("\n"))
    assert wrapped.replace("\n", " ") == text                # no word dropped or reordered


# --- shapes place the right actor set ---------------------------------------------------------


def test_sphere_shape_places_one_plane_one_sphere_one_label():
    editor = FakeEditor()
    module = _load(editor)
    material_path = "/ElysiumBaked/Materials/metal/MI_fridge"
    editor.assets[material_path] = FakeAsset(material_path)
    entry = _entry("Chrome fixture", "vtmb:material:metal/fridge", material_path, "sphere")

    placed = module.place_entry(editor.actor_subsystem, 0, entry)

    assert placed is True
    classes = [a.class_name for a in editor.actors]
    assert classes == ["StaticMeshActor", "StaticMeshActor", "TextRenderActor"]
    plane, sphere, label = editor.actors
    assert plane.location == (0.0, 0.0, 0.0)
    assert sphere.location == (0.0, 0.0, module.SPHERE_Z)
    assert plane.static_mesh_component.props["materials"][0].path == material_path
    assert sphere.static_mesh_component.props["materials"][0].path == material_path
    assert label.text_render.props["text"] == "Chrome fixture"


def test_plane_shape_places_only_one_plane_and_a_label():
    editor = FakeEditor()
    module = _load(editor)
    material_path = "/ElysiumBaked/Materials/tile/MI_apaflra"
    editor.assets[material_path] = FakeAsset(material_path)
    entry = _entry("Tiled floor", "vtmb:material:tile/apaflra", material_path, "plane")

    module.place_entry(editor.actor_subsystem, 1, entry)

    classes = [a.class_name for a in editor.actors]
    assert classes == ["StaticMeshActor", "TextRenderActor"]


def test_label_actor_is_rotated_to_face_the_aisle_and_sits_in_front_not_under():
    editor = FakeEditor()
    module = _load(editor)
    material_path = "/ElysiumBaked/Materials/tile/MI_apaflra"
    editor.assets[material_path] = FakeAsset(material_path)
    entry = _entry("Tiled floor", "vtmb:material:tile/apaflra", material_path, "plane")

    module.place_entry(editor.actor_subsystem, 1, entry)

    label = next(a for a in editor.actors if a.class_name == "TextRenderActor")
    x, y = module.grid_position(1)
    assert label.location == (x, y - module.LABEL_FRONT_OFFSET_CM, module.LABEL_Z_CM)
    assert label.location[2] > 0.0                            # above the floor, not under it
    assert label.rotation == (0.0, module.LABEL_YAW_DEGREES, 0.0)
    assert label.text_render.props["horizontal_alignment"] == "EHTA_CENTER"


# --- missing asset is a loud placeholder, not a crash -------------------------------------------


def test_missing_material_is_a_loud_placeholder_not_a_crash():
    editor = FakeEditor()
    module = _load(editor)
    entry = _entry(
        "Ghost material", "vtmb:material:nowhere/absent",
        "/ElysiumBaked/Materials/nowhere/MI_absent", "plane")

    placed = module.place_entry(editor.actor_subsystem, 3, entry)

    assert placed is False
    plane = editor.actors[0]
    assert (plane.static_mesh_component.props["materials"][0].path
            == module.PLACEHOLDER_MATERIAL_PATH)
    assert len(editor.warnings) == 1
    assert "vtmb:material:nowhere/absent" in editor.warnings[0]
    label = next(a for a in editor.actors if a.class_name == "TextRenderActor")
    assert "[MISSING]" in label.text_render.props["text"]


# --- props row (R1.6) -----------------------------------------------------------------------


def test_props_row_position_continues_the_grid_pitch_below_start_row():
    editor = FakeEditor()
    module = _load(editor)
    assert module.props_row_position(0, 3) == (0.0, 900.0)          # row 3, column 0
    assert module.props_row_position(1, 3) == (300.0, 900.0)
    assert module.props_row_position(8, 3) == (0.0, 1200.0)         # wraps into row 4


def test_place_prop_entry_spawns_the_mesh_alone_with_no_plane_or_sphere():
    editor = FakeEditor()
    module = _load(editor)
    mesh_path = "/ElysiumBaked/Meshes/SM_models_scenery_furniture_bench_bencha"
    editor.assets[mesh_path] = FakeAsset(mesh_path)
    entry = _prop_entry("Bench", "vtmb:model:scenery/furniture/bench/bencha", mesh_path)

    placed = module.place_prop_entry(editor.actor_subsystem, 0, entry, 3)

    assert placed is True
    classes = [a.class_name for a in editor.actors]
    assert classes == ["StaticMeshActor", "TextRenderActor"]        # no plane, no separate sphere
    mesh_actor, label = editor.actors
    assert mesh_actor.static_mesh_component.props["static_mesh"].path == mesh_path
    assert "materials" not in mesh_actor.static_mesh_component.props  # no material override
    assert label.text_render.props["text"] == "Bench"


def test_place_prop_entry_missing_mesh_is_a_loud_placeholder_sphere():
    editor = FakeEditor()
    module = _load(editor)
    entry = _prop_entry(
        "Ghost prop", "vtmb:model:nowhere/absent", "/ElysiumBaked/Meshes/SM_nowhere_absent")

    placed = module.place_prop_entry(editor.actor_subsystem, 0, entry, 3)

    assert placed is False
    sphere = editor.actors[0]
    assert sphere.static_mesh_component.props["materials"][0].path == module.PLACEHOLDER_MATERIAL_PATH
    assert len(editor.warnings) == 1
    assert "vtmb:model:nowhere/absent" in editor.warnings[0]
    label = next(a for a in editor.actors if a.class_name == "TextRenderActor")
    assert "[MISSING]" in label.text_render.props["text"]


def test_build_with_no_props_set_path_places_no_props_row(tmp_path):
    """The default (`props_set_path=None`) shape every pre-existing caller relies on."""
    editor = FakeEditor()
    module = _load(editor)
    material_path = "/ElysiumBaked/Materials/plaster/MI_609stuc"
    editor.assets[material_path] = FakeAsset(material_path)
    set_path = _write_set(tmp_path, [
        _entry("Plaster wall", "vtmb:material:plaster/609stuc", material_path, "sphere")])

    placed, missing = module.build(
        str(set_path), "/ElysiumBaked/Lookdev/Materials", str(tmp_path / "lookdev_report.json"))

    assert (placed, missing) == (1, 0)
    assert not any((a.label or "").startswith("Prop_") for a in editor.actors)


def test_build_places_a_props_row_below_the_material_grid(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    material_path = "/ElysiumBaked/Materials/plaster/MI_609stuc"
    editor.assets[material_path] = FakeAsset(material_path)
    set_path = _write_set(tmp_path, [
        _entry("Plaster wall", "vtmb:material:plaster/609stuc", material_path, "sphere")])
    mesh_path = "/ElysiumBaked/Meshes/SM_models_scenery_furniture_bench_bencha"
    editor.assets[mesh_path] = FakeAsset(mesh_path)
    props_set_path = _write_props_set(
        tmp_path, [_prop_entry("Bench", "vtmb:model:scenery/furniture/bench/bencha", mesh_path)])

    placed, missing = module.build(
        str(set_path), "/ElysiumBaked/Lookdev/Materials", str(tmp_path / "lookdev_report.json"),
        str(props_set_path))

    assert (placed, missing) == (2, 0)                                # 1 material bay + 1 prop
    prop_actor = next(a for a in editor.actors if a.label == "Prop_00_Bench")
    # start_row = ceil(1/8) + PROPS_ROW_GAP = 1 + 1 = 2, one row below the single-bay grid.
    assert prop_actor.location == (0.0, 2 * module.GRID_PITCH_CM, 0.0)
    report = json.loads((tmp_path / "lookdev_report.json").read_text(encoding="utf-8"))
    assert report["propsPlaced"] == 1
    assert report["propsMissing"] == []


# --- WorldSettings.default_game_mode is the boot-plan bypass ------------------------------------


def test_build_sets_default_game_mode_and_saves(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    material_path = "/ElysiumBaked/Materials/plaster/MI_609stuc"
    editor.assets[material_path] = FakeAsset(material_path)
    set_path = _write_set(tmp_path, [
        _entry("Plaster wall", "vtmb:material:plaster/609stuc", material_path, "sphere"),
        _entry("Ghost", "vtmb:material:nowhere/absent",
               "/ElysiumBaked/Materials/nowhere/MI_absent", "plane"),
    ])

    placed, missing = module.build(
        str(set_path), "/ElysiumBaked/Lookdev/Materials", str(tmp_path / "lookdev_report.json"))

    assert (placed, missing) == (1, 1)
    world = editor.worlds[-1]
    assert world.world_settings.props["default_game_mode"] is module.unreal.GameModeBase
    assert editor.saved == [(world, "/ElysiumBaked/Lookdev/Materials")]

    # The rig landed too: one of each fixed actor class, beyond the two review-set bays.
    rig_classes = [a.class_name for a in editor.actors if a.class_name not in (
        "StaticMeshActor", "TextRenderActor")]
    assert rig_classes.count("SkyLight") == 1
    assert rig_classes.count("DirectionalLight") == 1
    assert rig_classes.count("PointLight") == 3
    assert rig_classes.count("PostProcessVolume") == 1
    assert rig_classes.count("PlayerStart") == 1


def test_build_writes_a_report_beside_placed_missing_and_actors(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    material_path = "/ElysiumBaked/Materials/plaster/MI_609stuc"
    editor.assets[material_path] = FakeAsset(material_path)
    set_path = _write_set(tmp_path, [
        _entry("Plaster wall", "vtmb:material:plaster/609stuc", material_path, "sphere"),
        _entry("Ghost", "vtmb:material:nowhere/absent",
               "/ElysiumBaked/Materials/nowhere/MI_absent", "plane"),
    ])
    report_path = tmp_path / "lookdev_report.json"

    placed, missing = module.build(
        str(set_path), "/ElysiumBaked/Lookdev/Materials", str(report_path))

    assert (placed, missing) == (1, 1)
    report = json.loads(report_path.read_text(encoding="utf-8"))
    assert report["map"] == "/ElysiumBaked/Lookdev/Materials"
    assert report["placed"] == 1
    assert [row["label"] for row in report["missing"]] == ["Ghost"]
    assert report["missing"][0]["unit"] == "vtmb:material:nowhere/absent"
    assert "PlayerStart" in report["actors"]


def test_post_process_volume_pins_manual_exposure_without_the_physical_camera():
    editor = FakeEditor()
    module = _load(editor)
    module.place_rig(editor.actor_subsystem)

    ppv = next(a for a in editor.actors if a.class_name == "PostProcessVolume")
    assert ppv.props["unbound"] is True
    settings = ppv.props["settings"]
    assert settings.props["bOverride_AutoExposureMethod"] is True
    assert settings.props["auto_exposure_method"] == "AEM_MANUAL"
    # The white point AEM_Manual computes comes from the physical-camera fields unless this is
    # off (PostProcessEyeAdaptation.cpp's CalculateManualAutoExposure) -- pinned off so the rig
    # is correct on its own, not because some other cvar happens to help it.
    assert settings.props["bOverride_AutoExposureApplyPhysicalCameraExposure"] is True
    assert settings.props["AutoExposureApplyPhysicalCameraExposure"] is False
    assert settings.props["bOverride_AutoExposureBias"] is True
    assert settings.props["AutoExposureBias"] == 0.0


def test_directional_light_pitch_and_intensity():
    editor = FakeEditor()
    module = _load(editor)
    module.place_rig(editor.actor_subsystem)

    sun = next(a for a in editor.actors if a.class_name == "DirectionalLight")
    assert sun.rotation == (-45.0, 0.0, 0.0)
    assert sun.light_component.props["intensity"] == 3.0
    assert sun.light_component.props["mobility"] == "MOVABLE"


def test_point_lights_are_centred_on_the_grid_and_reach_across_it():
    editor = FakeEditor()
    module = _load(editor)
    centroid = (900.0, 300.0)

    module.place_rig(editor.actor_subsystem, centroid)

    points = [a for a in editor.actors if a.class_name == "PointLight"]
    assert len(points) == 3
    cx, cy = centroid
    for point, (px, py, pz) in zip(points, module.POINT_LIGHT_OFFSETS_CM):
        assert point.location == (cx + px, cy + py, pz)
        assert point.light_component.props["mobility"] == "MOVABLE"
        assert (point.light_component.props["attenuation_radius"]
                == module.POINT_LIGHT_ATTENUATION_RADIUS_CM)


def test_player_start_location():
    editor = FakeEditor()
    module = _load(editor)
    module.place_rig(editor.actor_subsystem)

    start = next(a for a in editor.actors if a.class_name == "PlayerStart")
    assert start.location == module.PLAYER_START_LOCATION


# --- refusals ------------------------------------------------------------------------------------


def test_build_raises_on_a_missing_set_file(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    with pytest.raises(OSError):
        module.build(str(tmp_path / "does-not-exist.json"), "/ElysiumBaked/Lookdev/Materials")


def test_build_raises_on_an_empty_entries_array(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    set_path = _write_set(tmp_path, [])
    with pytest.raises(ValueError):
        module.build(str(set_path), "/ElysiumBaked/Lookdev/Materials")


def test_main_exits_non_zero_when_save_map_fails(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    material_path = "/ElysiumBaked/Materials/plaster/MI_609stuc"
    editor.assets[material_path] = FakeAsset(material_path)
    set_path = _write_set(
        tmp_path, [_entry("Plaster wall", "vtmb:material:plaster/609stuc", material_path, "sphere")])
    editor.command_line = '"-LookdevSet=%s" "-LookdevMap=/ElysiumBaked/Lookdev/Materials"' % set_path
    editor.save_map_returns = False

    with pytest.raises(SystemExit) as failed:
        module.main()
    assert failed.value.code == 1
    assert editor.errors


def test_main_exits_non_zero_when_entries_are_missing(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    set_path = _write_set(tmp_path, [
        _entry("Ghost", "vtmb:material:nowhere/absent",
               "/ElysiumBaked/Materials/nowhere/MI_absent", "plane"),
    ])
    editor.command_line = '"-LookdevSet=%s" "-LookdevMap=/ElysiumBaked/Lookdev/Materials"' % set_path

    with pytest.raises(SystemExit) as failed:
        module.main()

    assert failed.value.code == 1
    assert editor.saved                                       # still saved before failing


def test_main_allows_missing_entries_with_the_override_flag(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    set_path = _write_set(tmp_path, [
        _entry("Ghost", "vtmb:material:nowhere/absent",
               "/ElysiumBaked/Materials/nowhere/MI_absent", "plane"),
    ])
    editor.command_line = (
        '"-LookdevSet=%s" "-LookdevMap=/ElysiumBaked/Lookdev/Materials" -LookdevAllowMissing=1'
        % set_path
    )

    module.main()                                             # does not raise

    assert editor.saved


def test_main_honours_command_line_overrides(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    material_path = "/ElysiumBaked/Materials/plaster/MI_609stuc"
    editor.assets[material_path] = FakeAsset(material_path)
    set_path = _write_set(
        tmp_path, [_entry("Plaster wall", "vtmb:material:plaster/609stuc", material_path, "sphere")])
    mesh_path = "/ElysiumBaked/Meshes/SM_models_scenery_furniture_ascandle_ascandle"
    editor.assets[mesh_path] = FakeAsset(mesh_path)
    props_set_path = _write_props_set(
        tmp_path, [_prop_entry("Candle", "vtmb:model:scenery/furniture/ascandle/ascandle", mesh_path)])
    report_path = tmp_path / "custom_report.json"
    editor.command_line = (
        '"-LookdevSet=%s" "-LookdevPropsSet=%s" "-LookdevMap=/Test/CustomMap" "-LookdevReport=%s"'
        % (set_path, props_set_path, report_path)
    )

    module.main()

    assert editor.saved and editor.saved[0][1] == "/Test/CustomMap"
    assert report_path.is_file()
