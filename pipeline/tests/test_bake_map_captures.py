"""R5.5: the
editor half of the capture placement, `pipeline/unreal/bake_map_v2.py`.

`capture_placement` is the one pure rule in that file -- a world sample stands where the unit put
it at the settings page's radius, a 3D-skybox sample takes the miniature's transform for both --
and the manifest version the lane refuses anything but must be the one the offline stage writes.
Loaded through the same fake-editor-module trick `test_bake_map_sky.py` uses, because the module
is an editor entry point that cannot otherwise import.
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
    """The editor module surface `bake_lib` touches at import (`test_bake_map_sky._fake_unreal`,
    restated: a test module is not an import target)."""
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
        "elysium_test_bake_map_v2", REPO / "pipeline/unreal/bake_map_v2.py")
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


def test_world_sample_stands_where_the_unit_put_it_at_the_page_radius(module) -> None:
    position, radius = module.capture_placement(
        (-406.4, -833.12, 129.54), False, 16.0, (1000.0, 2000.0, 3000.0), 1500.0)
    assert position == pytest.approx((-406.4, -833.12, 129.54))
    assert radius == 1500.0


def test_sky_sample_takes_the_miniature_transform_for_position_and_radius(module) -> None:
    # The same rule a miniature light takes: `scale * (p - sky_origin)` and reach `* scale`, so a
    # capture authored in the 1/16 miniature covers what it covered once the miniature is blown up.
    position, radius = module.capture_placement(
        (110.0, -20.0, 5.0), True, 16.0, (100.0, -30.0, 0.0), 1500.0)
    assert position == pytest.approx((160.0, 160.0, 80.0))
    assert radius == pytest.approx(24000.0)


def test_editor_lane_reads_the_manifest_version_the_stage_writes(module) -> None:
    # The invariant is that the two halves agree, not the number they agree on: the
    # constant is restated across the numpy boundary and bumps whenever a table is added
    # (R7.2 took it to 8), and a literal here only teaches the next bump to edit it here too.
    assert module.MANIFEST_VERSION == MG.MANIFEST_VERSION
    sample = module._CubemapSample(
        {"index": 3, "origin": [1, -2, 3], "position": [2.54, 5.08, 7.62], "sky": False})
    assert (sample.index, sample.origin, sample.position, sample.sky) == (
        3, (1, -2, 3), (2.54, 5.08, 7.62), False)
