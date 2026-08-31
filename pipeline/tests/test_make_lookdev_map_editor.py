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
        GameModeBase=type("GameModeBase", (), {}),
    )


def _load(editor):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_make_lookdev_map", REPO / "pipeline/unreal/make_lookdev_map.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    fake = _fake_unreal(editor)
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        spec.loader.exec_module(module)   # main() runs once at import, against the tracked set
    module.unreal = fake
    # The import-time run is real (it reads the tracked, committed lookdev_set.json against an
    # empty fake asset registry) but is not any one test's scenario -- start each test clean.
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


# --- the default tracked set loads and runs cleanly at import ------------------------------------


def test_module_import_runs_the_default_tracked_set():
    editor = FakeEditor()
    # Before _load()'s own cleanup, the import-time `main()` call already built the tracked,
    # committed review set (no -LookdevSet= override) against an editor with no assets at all --
    # every entry lands as a placeholder, and the map still saves. Check that directly.
    spec = importlib.util.spec_from_file_location(
        "elysium_test_make_lookdev_map_default", REPO / "pipeline/unreal/make_lookdev_map.py")
    module = importlib.util.module_from_spec(spec)
    fake = _fake_unreal(editor)
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        spec.loader.exec_module(module)
    tracked = json.loads((REPO / "pipeline/unreal/lookdev_set.json").read_text(encoding="utf-8"))
    entry_count = len(tracked["entries"])
    assert len(editor.warnings) == entry_count      # every entry missing against an empty registry
    assert editor.saved == [(editor.worlds[0], module.DEFAULT_MAP_PATH)]


# --- grid positions are deterministic --------------------------------------------------------


def test_grid_positions_are_deterministic():
    editor = FakeEditor()
    module = _load(editor)
    assert module.grid_position(0) == (0.0, 0.0)
    assert module.grid_position(1) == (300.0, 0.0)
    assert module.grid_position(7) == (2100.0, 0.0)
    assert module.grid_position(8) == (0.0, 300.0)         # wraps after GRID_COLUMNS
    assert module.grid_position(9) == (300.0, 300.0)


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


def test_both_shape_places_two_planes_and_a_sphere():
    editor = FakeEditor()
    module = _load(editor)
    material_path = "/ElysiumBaked/Materials/plaster/MI_609stuc"
    editor.assets[material_path] = FakeAsset(material_path)
    entry = _entry("Plaster wall", "vtmb:material:plaster/609stuc", material_path, "both")

    module.place_entry(editor.actor_subsystem, 2, entry)

    classes = [a.class_name for a in editor.actors]
    assert classes == ["StaticMeshActor", "StaticMeshActor", "StaticMeshActor", "TextRenderActor"]
    first_plane, sphere, second_plane, _label = editor.actors
    assert first_plane.location == (600.0, 0.0, 0.0)
    assert second_plane.location == (600.0, 0.0 + module.SECOND_PLANE_OFFSET_CM, 0.0)


# --- missing asset is a labelled placeholder, not a crash --------------------------------------


def test_missing_material_is_a_placeholder_not_a_crash():
    editor = FakeEditor()
    module = _load(editor)
    entry = _entry(
        "Ghost material", "vtmb:material:nowhere/absent",
        "/ElysiumBaked/Materials/nowhere/MI_absent", "plane")

    placed = module.place_entry(editor.actor_subsystem, 3, entry)

    assert placed is False
    plane = editor.actors[0]
    assert "materials" not in plane.static_mesh_component.props
    assert len(editor.warnings) == 1
    assert "vtmb:material:nowhere/absent" in editor.warnings[0]


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

    placed, missing = module.build(str(set_path), "/ElysiumBaked/Lookdev/Materials")

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


def test_post_process_volume_pins_manual_exposure_unbound():
    editor = FakeEditor()
    module = _load(editor)
    module.place_rig(editor.actor_subsystem)

    ppv = next(a for a in editor.actors if a.class_name == "PostProcessVolume")
    assert ppv.props["unbound"] is True
    settings = ppv.props["settings"]
    assert settings.props["auto_exposure_method"] == "AEM_MANUAL"
    assert settings.props["auto_exposure_min_brightness"] == 1.0
    assert settings.props["auto_exposure_max_brightness"] == 1.0


def test_directional_light_pitch_and_intensity():
    editor = FakeEditor()
    module = _load(editor)
    module.place_rig(editor.actor_subsystem)

    sun = next(a for a in editor.actors if a.class_name == "DirectionalLight")
    assert sun.rotation == (-45.0, 0.0, 0.0)
    assert sun.light_component.props["intensity"] == 3.0
    assert sun.light_component.props["mobility"] == "MOVABLE"


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


def test_main_honours_command_line_overrides(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    material_path = "/ElysiumBaked/Materials/plaster/MI_609stuc"
    editor.assets[material_path] = FakeAsset(material_path)
    set_path = _write_set(
        tmp_path, [_entry("Plaster wall", "vtmb:material:plaster/609stuc", material_path, "sphere")])
    editor.command_line = '"-LookdevSet=%s" "-LookdevMap=/Test/CustomMap"' % set_path

    module.main()

    assert editor.saved and editor.saved[0][1] == "/Test/CustomMap"
