"""R6.3: the V2 map bake instances
every `dprp` record off the staged `details` table.

Two things in that section can change shipped content without any other test noticing: the
**grouping** (`detail_instance_rows` decides which component a record lands in and at which
instance index, and `bake_verify` counts them back by exactly that rule) and the **row layout** the
offline stage writes and the editor half reads positionally (a shifted column is a map full of
weeds at the wrong height with every count still matching). Both halves share one manifest
version, pinned here too.
"""

from __future__ import annotations

import importlib.util
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
    """The editor module surface `bake_lib` touches at import (`test_bake_map_lights`)."""
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
        "elysium_test_bake_map_v2_details", REPO / "pipeline/unreal/bake_map_v2.py")
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


def _detail(index, model, stem, sway=0, sky=False, position=(0.0, 0.0, 0.0)):
    return MG.DetailPlacement(
        index=index, model=model, stem=stem, model_path=f"models/{stem}.mdl",
        position=position, rotation=(0.0, 0.0, 0.0, 1.0), sway=sway, sky=sky)


def test_detail_instance_rows_group_by_model_in_lump_order_and_normalise_sway(module) -> None:
    details = [
        _detail(0, 1, "grassa", sway=0),
        _detail(1, 0, "rock", sway=255),
        _detail(2, 1, "grassa", sway=51, position=(10.0, 20.0, 30.0)),
        _detail(3, 1, "grassa", sway=1),
    ]
    groups = module.detail_instance_rows(details)
    # One component per model, in first-record order; instance k is the model's k-th record.
    assert list(groups) == [("vtmb:model:grassa", False), ("vtmb:model:rock", False)]
    grass = groups[("vtmb:model:grassa", False)]
    assert [row[0] for row in grass] == [(0.0, 0.0, 0.0), (10.0, 20.0, 30.0), (0.0, 0.0, 0.0)]
    # `swayAmount / 255`; an unswayed record is an exact 0, never a rounding residue.
    assert [row[3] for row in grass] == [0.0, pytest.approx(0.2), pytest.approx(1.0 / 255.0)]
    assert grass[0][3] == 0.0
    assert groups[("vtmb:model:rock", False)][0][3] == 1.0
    # A world instance is unscaled; rotation rides through untouched.
    assert all(row[2] == 1.0 for row in grass)
    assert grass[0][1] == (0.0, 0.0, 0.0, 1.0)


def test_a_miniature_detail_takes_the_sky_transform_in_its_own_component(module) -> None:
    details = [
        _detail(0, 0, "weed", position=(1010.0, 2020.0, 3030.0)),
        _detail(1, 0, "weed", sky=True, position=(1010.0, 2020.0, 3030.0)),
    ]
    groups = module.detail_instance_rows(details, sky_scale=16.0, sky_origin=(1000.0, 2000.0, 3000.0))
    assert list(groups) == [("vtmb:model:weed", False), ("vtmb:model:weed", True)]
    assert groups[("vtmb:model:weed", False)][0][0] == (1010.0, 2020.0, 3030.0)
    sky_row = groups[("vtmb:model:weed", True)][0]
    assert sky_row[0] == pytest.approx((160.0, 320.0, 480.0))
    assert sky_row[2] == 16.0


def test_the_staged_row_layout_round_trips_between_the_two_halves(module) -> None:
    # The offline stage writes positionally (`DETAIL_RECORD_FIELDS`) and the editor half reads
    # positionally (`_DetailPlacement`); the two must agree column for column, and on the version.
    # The invariant is that the two halves agree, not the number they agree on: the
    # constant is restated across the numpy boundary and bumps whenever a table is added
    # (R7.2 took it to 8), and a literal here only teaches the next bump to edit it here too.
    assert module.MANIFEST_VERSION == MG.MANIFEST_VERSION
    detail = MG.DetailPlacement(
        index=7, model=2, stem="weedc", model_path="models/scenery/plants/weedc/weedc.mdl",
        position=(1.5, -2.5, 3.5), rotation=(0.1, 0.2, 0.3, 0.9), sway=42, sky=True)
    row = MG.detail_record_row(detail)
    assert len(row) == len(MG.DETAIL_RECORD_FIELDS) == 11
    staged = module._DetailPlacement(row, {2: {"stem": "weedc", "modelPath": detail.model_path}})
    assert staged.model_path == detail.model_path
    assert (staged.index, staged.model, staged.stem) == (7, 2, "weedc")
    assert staged.position == (1.5, -2.5, 3.5)
    assert staged.rotation == (0.1, 0.2, 0.3, 0.9)
    assert (staged.sway, staged.sky) == (42, True)
    assert staged.as_row() == [7, 2, [1.5, -2.5, 3.5], [0.1, 0.2, 0.3, 0.9], 42, 1]
