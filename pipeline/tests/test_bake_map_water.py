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


def _fake_unreal():
    """The editor module surface `bake_lib` touches at import (`test_bake_map_sprites.py`)."""
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
        LinearColor=lambda *values: values,
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
    assert module.WATER_ACTOR_SHAPE == 1
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
