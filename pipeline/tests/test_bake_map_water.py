"""R7.1 (`docs/architecture/water-architecture.md` -> section 5, "The volume: `water.volumes[]`"
and section 5.2, "The actor"): the V2 map bake folds every staged `water.volumes[]` row into the
one `AElysiumWaterVolumes` actor.

What can change shipped content without any other test noticing: the **placement** the editor
half writes for the one actor (its position, label, folder and tag -- `water_actor_values`), the
**tolerance** of a manifest staged before this lane existed (no `"water"` key at all, or a map
that never places a volume), the **shape/tag** constants the recipe and the runtime both key on,
and the manifest version the two halves share. Loaded through the same fake-editor-module trick
`test_bake_map_sprites.py` uses, because the module is an editor entry point that cannot
otherwise import.
"""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
from unittest import mock

import pytest

from elysium_pipeline.importers import map_geometry as MG

REPO = Path(__file__).resolve().parents[2]


class FakeStruct:
    """A dict-backed stand-in for a `unreal` struct instance -- close enough to the real reflection
    wrapper for `Bake._set`'s `set_editor_property`/`get_editor_property` round trip to exercise
    the R7.4 `fluid`/`pieces`/`leaf_boxes_cm`/`near_boxes_cm` writes with no live editor."""

    def __init__(self, **defaults):
        self._data = dict(defaults)

    def set_editor_property(self, name, value):
        self._data[name] = value

    def get_editor_property(self, name):
        return self._data[name]

    def get_class(self):
        return SimpleNamespace(get_name=lambda: type(self).__name__)


class FakeActor(FakeStruct):
    """`FakeStruct` plus the handful of `AActor` calls `_place_water` makes."""

    def __init__(self):
        FakeStruct.__init__(self)
        self.tags = []

    def set_actor_label(self, label):
        self._data["label"] = label

    def set_folder_path(self, path):
        self._data["folder"] = path


def _fake_unreal():
    """The editor module surface `bake_lib` touches at import (`test_bake_map_sprites.py`), plus
    the R7.1/R7.4 water struct constructors `_place_water` calls directly."""
    editor = SimpleNamespace(
        does_directory_exist=lambda target: True,
        make_directory=lambda target: True,
        does_asset_exist=lambda target: False,
        load_asset=lambda target: None,
        list_assets=lambda package, recursive=True, include_folder=True: [],
    )
    return SimpleNamespace(
        AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
        MaterialEditingLibrary=object(),
        GeometryScript_Collision=object(),
        EditorAssetLibrary=editor,
        Paths=SimpleNamespace(project_dir=lambda: str(REPO)),
        SystemLibrary=SimpleNamespace(get_command_line=lambda: ""),
        LinearColor=lambda *values: FakeStruct(
            r=values[0], g=values[1], b=values[2],
            a=values[3] if len(values) > 3 else 1.0),
        Plane=lambda x, y, z, w: FakeStruct(x=x, y=y, z=z, w=w),
        Vector=lambda x=0.0, y=0.0, z=0.0: FakeStruct(x=x, y=y, z=z),
        Box=lambda min=None, max=None: FakeStruct(min=min, max=max, is_valid=0),
        ElysiumWaterVolumes=object,
        ElysiumWaterVolume=FakeStruct,
        ElysiumWaterBrush=FakeStruct,
        ElysiumWaterFluid=FakeStruct,
        log=lambda *a, **k: None,
        log_warning=lambda *a, **k: None,
        log_error=lambda *a, **k: None,
    )


def _load_bake_map_v2(export_root: str):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_bake_map_v2_water", REPO / "pipeline/unreal/bake_map_v2.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"unreal": _fake_unreal()}), \
            mock.patch.dict(os.environ, {"ELYSIUM_EXPORT_ROOT": export_root}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        spec.loader.exec_module(module)
    return module


@pytest.fixture(scope="module")
def module():
    with tempfile.TemporaryDirectory() as out:
        yield _load_bake_map_v2(out)


def _volume(**overrides):
    row = {
        "index": 0, "surfaceZCm": -14937.74, "minZCm": -14988.54,
        "material": "vtmb:material:water/sewer_water",
        "fogEnable": True, "fogColor": [0.0196, 0.0196, 0.0],
        "fogStartCm": 2.54, "fogEndCm": 2600.96,
        "brushes": [{"planes": [[0.0, 0.0, 1.0, -14937.74]],
                     "boundsCm": {"min": [-100.0, -200.0, -14988.54],
                                  "max": [100.0, 200.0, -14937.74]}}],
    }
    row.update(overrides)
    return row


def test_water_actor_values_position_label_folder_tags(module) -> None:
    values = module.water_actor_values([_volume()])
    # The bounds centre of the first volume's own brush(es) -- the mid-point of the one authored
    # brush here, not the surface plane or the origin.
    assert values["position"] == pytest.approx((0.0, 0.0, -14963.14))
    assert values["label"] == "WaterVolumes"
    assert values["folder"] == "Water"
    assert values["tags"] == ("elysium.water",)
    assert values["tags"] == (module.TAG_WATER,)


def test_water_actor_values_merges_every_brush_of_the_first_volume(module) -> None:
    # Two brushes under one volume (a patched-water unit's own row, or a leaf whose brush split):
    # the actor still stands at the merged bounds, not just the first brush's.
    volume = _volume(brushes=[
        {"planes": [], "boundsCm": {"min": [-100.0, -100.0, -50.0], "max": [0.0, 0.0, 0.0]}},
        {"planes": [], "boundsCm": {"min": [0.0, 0.0, -50.0], "max": [100.0, 100.0, 0.0]}},
    ])
    values = module.water_actor_values([volume])
    assert values["position"] == pytest.approx((0.0, 0.0, -25.0))


def test_water_actor_values_falls_back_to_the_surface_when_the_volume_has_no_brush(module) -> None:
    # A row with no matched brush is dropped by the stage lane (`water-architecture.md` section
    # 5.1), but the placement rule stays total: no crash on a volume carrying an empty list.
    values = module.water_actor_values([_volume(brushes=[])])
    assert values["position"] == pytest.approx((0.0, 0.0, -14937.74))


def _write_manifest(directory: str, module, water=None):
    manifest = {
        "schema": module.MANIFEST_SCHEMA, "version": module.MANIFEST_VERSION,
        "vertexFile": "vertices.bin", "vertexBytes": 0,
        "unit": "vtmb:map:fake", "unitSha256": "0" * 64,
        "counts": {}, "sky": {"scale": 16.0, "origin": [0.0, 0.0, 0.0], "ok": True},
        "placements": [], "materials": {},
    }
    if water is not None:
        manifest["water"] = water
    manifest_path = os.path.join(directory, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle)
    with open(os.path.join(directory, "vertices.bin"), "wb"):
        pass
    return manifest_path


def test_staged_geometry_water_tolerates_a_manifest_with_no_water_key(module) -> None:
    # A manifest staged before the stage-geometry lane's own bump carries no "water" key at all;
    # `_place_water` must read that the same as a map with a genuinely empty `LEAFWATERDATA`.
    with tempfile.TemporaryDirectory() as directory:
        manifest_path = _write_manifest(directory, module, water=None)
        geometry = module._StagedGeometry(manifest_path)
    assert geometry.error == ""
    assert geometry.water == []


def test_staged_geometry_water_carries_the_staged_rows_verbatim(module) -> None:
    # `_level_recipe` writes `recipe["water"] = self.geometry.water` with no transform of its
    # own, so this table is the recipe's whole water input: it must round-trip untouched.
    rows = [_volume(index=0), _volume(index=1, material="vtmb:material:water/invisible_water")]
    with tempfile.TemporaryDirectory() as directory:
        manifest_path = _write_manifest(directory, module, water={"volumes": rows, "dropped": []})
        geometry = module._StagedGeometry(manifest_path)
    assert geometry.error == ""
    assert geometry.water == rows


def test_water_actor_shape_and_tag_constants(module) -> None:
    # The shape the recipe pins the level against (`bake_map_v2.WATER_ACTOR_SHAPE`), and the tag
    # `bake_map.TAG_WATER` restates for the pure placement functions.
    assert module.WATER_ACTOR_SHAPE == 2
    assert module.TAG_WATER == "elysium.water"


def test_zero_row_manifest_places_nothing(module) -> None:
    # No staged volume: `_place_water` returns 0 and never asks the editor to spawn an actor --
    # "no rows, no actor" (`water-architecture.md` section 5.2).
    module.bind({"Bake": object, "log": lambda *a, **k: None, "fail": lambda *a, **k: None})
    klass = module.bake_class()
    instance = klass.__new__(klass)
    instance.geometry = SimpleNamespace(water=[])
    spawned = []
    actors = SimpleNamespace(
        spawn_actor_from_class=lambda *a, **k: spawned.append((a, k)) or None)
    placed = instance._place_water(actors)
    assert placed == 0
    assert spawned == []


def test_the_two_halves_share_the_manifest_version(module) -> None:
    # The invariant is that the two halves agree, not the number they agree on: the constant is
    # restated across the numpy boundary and bumps whenever a table is added (R7.1 took it to 9),
    # and a literal here only teaches the next bump to edit it here too.
    assert module.MANIFEST_VERSION == MG.MANIFEST_VERSION


# --------------------------------------------------------------------- R7.4 contract 1: underside

def _material_row(**overrides):
    row = {
        "unit": "vtmb:material:water/sewer_water", "asset": "/ElysiumBaked/Materials/water/MI_sewer_water",
        "master": "M_V2_Water", "blendMode": "Opaque", "opaque": True, "patched": False,
        "isDecalSurface": False, "decalAsset": None, "underside": False, "undersideAsset": None,
        "lightStyle": None,
    }
    row.update(overrides)
    return row


def test_v2_material_slot_asset_is_the_surface_instance_by_default(module) -> None:
    mat = module._V2Material("water/sewer_water", _material_row())
    assert mat.underside is False
    assert mat.underside_asset is None
    assert mat.slot_asset == "/ElysiumBaked/Materials/water/MI_sewer_water"


def test_v2_material_slot_asset_binds_the_staged_underside_twin(module) -> None:
    # Contract 1: `map_geometry.MaterialBinding.underside_asset` already computed the twin path
    # offline (`<asset>_Underside`) and staged it as `undersideAsset` -- this lane reads it
    # verbatim rather than recomputing a naming rule Lane C owns.
    row = _material_row(underside=True,
                        undersideAsset="/ElysiumBaked/Materials/water/MI_sewer_water_Underside")
    mat = module._V2Material("water/sewer_water#underside", row)
    assert mat.underside is True
    assert mat.slot_asset == "/ElysiumBaked/Materials/water/MI_sewer_water_Underside"


def test_v2_material_falls_back_to_the_naming_rule_when_undersideasset_is_absent(module) -> None:
    # Total against an older-staged manifest that set `underside: true` without precomputing the
    # twin path -- `underside_asset_path` restates Lane C's own `<asset>_Underside` rule.
    row = _material_row(underside=True, undersideAsset=None)
    mat = module._V2Material("water/sewer_water#underside", row)
    assert mat.slot_asset == module.underside_asset_path(
        "/ElysiumBaked/Materials/water/MI_sewer_water")


def test_v2_material_decal_surface_still_wins_over_underside(module) -> None:
    # A face group cannot be both a `$decal` surface and a water underside in this corpus, but the
    # precedence is stated so it never has to be discovered by a failing bake.
    row = _material_row(isDecalSurface=True, decalAsset="/ElysiumBaked/Materials/MI_x_Decal",
                        underside=True, undersideAsset="/ElysiumBaked/Materials/MI_x_Underside")
    mat = module._V2Material("x#underside", row)
    assert mat.slot_asset == "/ElysiumBaked/Materials/MI_x_Decal"


def test_v2_material_light_style_defaults_to_zero(module) -> None:
    assert module._V2Material("k", _material_row()).light_style == 0
    assert module._V2Material("k", _material_row(lightStyle=1)).light_style == 1


# ------------------------------------------------------------- R7.4 contract 4: fluid/pieces/boxes

def test_place_water_writes_fluid_pieces_and_boxes_when_staged(module) -> None:
    module.bind({"Bake": object, "log": lambda *a, **k: None, "fail": lambda *a, **k: None})
    klass = module.bake_class()
    instance = klass.__new__(klass)
    row = _volume(
        fluid={"index": 5, "density": 1000.0, "damping": 0.1, "surfacePlane": [0, 0, 1, -14937.74],
              "currentVelocityCm": [0.0, 0.0, 0.0], "contents": 0x4000010},
        pieces=[{"planes": [[0.0, 0.0, 1.0, -14937.74], [1.0, 0.0, 0.0, 50.0]]}],
        leafBoxesCm=[{"min": [-10.0, -10.0, -14988.0], "max": [10.0, 10.0, -14937.0]}],
        nearBoxesCm=[{"min": [-500.0, -500.0, -15100.0], "max": [500.0, 500.0, -14900.0]}],
    )
    instance.geometry = SimpleNamespace(water=[row])
    captured = []
    actors = SimpleNamespace(
        spawn_actor_from_class=lambda *a, **k: captured.append(FakeActor()) or captured[-1])
    placed = instance._place_water(actors)
    assert placed == 1
    actor = captured[0]
    volumes = actor.get_editor_property("volumes")
    assert len(volumes) == 1
    fluid = volumes[0].get_editor_property("fluid")
    assert fluid.get_editor_property("has_fluid") is True
    assert fluid.get_editor_property("index") == 5
    assert fluid.get_editor_property("density") == pytest.approx(1000.0)
    assert fluid.get_editor_property("contents") == 0x4000010
    assert len(volumes[0].get_editor_property("pieces")) == 1
    assert len(volumes[0].get_editor_property("leaf_boxes_cm")) == 1
    assert len(volumes[0].get_editor_property("near_boxes_cm")) == 1


def test_place_water_omits_optional_fields_when_the_manifest_carries_none(module) -> None:
    # An older-staged manifest (pre-R7.4) carries no `fluid`/`pieces`/box keys at all -- `_set` is
    # never called for them, so a `unreal.ElysiumWaterVolume` built before Lane D's own bump still
    # bakes instead of hard-failing on an unknown property.
    module.bind({"Bake": object, "log": lambda *a, **k: None, "fail": lambda *a, **k: None})
    klass = module.bake_class()
    instance = klass.__new__(klass)
    instance.geometry = SimpleNamespace(water=[_volume()])
    captured = []
    actors = SimpleNamespace(
        spawn_actor_from_class=lambda *a, **k: captured.append(FakeActor()) or captured[-1])
    instance._place_water(actors)
    volume = captured[0].get_editor_property("volumes")[0]
    assert "fluid" not in volume._data
    assert "pieces" not in volume._data
    assert "leaf_boxes_cm" not in volume._data
    assert "near_boxes_cm" not in volume._data
