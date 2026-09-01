"""R2.3 (MP-1.3): `level_sidecar_recipe` closes over the runtime-only sidecars.

`.ents`, `.hulls`, `.dispcol` and `.ropes` feed no baked actor -- `AElysiumMapActor` reads them at
map load, not the bake -- so nothing else in the recipe notices a touched one. Without a digest
here the tracker would keep serving a level stamped against an input that no longer matches: a
stale level that reads as a runtime bug (`docs/project/seam_migration.md`, R2.3/MP-1.3).
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

REPO = Path(__file__).resolve().parents[2]


def _fake_unreal():
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
        AssetRegistryHelpers=SimpleNamespace(
            get_asset_registry=mock.Mock(side_effect=SystemExit(0))),
        LinearColor=lambda *values: values,
        log=lambda *a, **k: None,
        log_warning=lambda *a, **k: None,
        log_error=lambda *a, **k: None,
    )


def _load_bake_map(export_root: str):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_bake_map_recipe", REPO / "pipeline/unreal/bake_map.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"unreal": _fake_unreal()}), \
            mock.patch.dict(os.environ, {"ELYSIUM_EXPORT_ROOT": export_root}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        try:
            # bake_map is an editor entry point, so importing it runs main(). The faked
            # registry scan exits it at once, with every definition made.
            spec.loader.exec_module(module)
        except SystemExit:
            pass
    return module


@pytest.fixture(scope="module")
def module():
    with tempfile.TemporaryDirectory() as out:
        yield _load_bake_map(out)


SIDECARS = ("ents", "hulls", "dispcol", "ropes")


def _write_map(root: Path, map_name: str, bodies: dict[str, str]) -> None:
    root.mkdir(parents=True, exist_ok=True)
    for suffix, body in bodies.items():
        (root / f"{map_name}.{suffix}").write_text(body, encoding="utf-8")


def test_all_four_runtime_sidecars_carry_a_digest(module) -> None:
    with tempfile.TemporaryDirectory() as out:
        root = Path(out)
        _write_map(root, "sp_test", {
            "ents": '{"entities": []}',
            "hulls": "0 0 0 0 0 0\n",
            "dispcol": "0 0 0 0 0 0 0 0 0\n",
            "ropes": "rope 0 0 0 1 1 1\n",
        })
        recipe = module.level_sidecar_recipe(root, "sp_test")
    sidecars = recipe["runtime_sidecars"]
    for suffix in SIDECARS:
        assert sidecars[f"{suffix}_sha256"] is not None
        assert len(sidecars[f"{suffix}_sha256"]) == 64


@pytest.mark.parametrize("suffix", SIDECARS)
def test_touching_one_sidecar_dirties_the_recipe_and_no_other_field(module, suffix) -> None:
    base = {
        "ents": '{"entities": []}',
        "hulls": "0 0 0 0 0 0\n",
        "dispcol": "0 0 0 0 0 0 0 0 0\n",
        "ropes": "rope 0 0 0 1 1 1\n",
    }
    with tempfile.TemporaryDirectory() as out:
        root = Path(out)
        _write_map(root, "sp_test", base)
        before = module.level_sidecar_recipe(root, "sp_test")

        touched = dict(base)
        touched[suffix] = touched[suffix] + "extra\n"
        _write_map(root, "sp_test", touched)
        after = module.level_sidecar_recipe(root, "sp_test")

    assert before != after
    assert before["runtime_sidecars"][f"{suffix}_sha256"] != \
        after["runtime_sidecars"][f"{suffix}_sha256"]
    # The three untouched sidecars keep their digest -- one changed file dirties only its own key.
    for other in SIDECARS:
        if other == suffix:
            continue
        assert before["runtime_sidecars"][f"{other}_sha256"] == \
            after["runtime_sidecars"][f"{other}_sha256"]
    # Nothing parsed elsewhere in the recipe moves -- these sidecars are runtime-only inputs.
    for key in ("props", "decals", "lights", "environment", "sky", "spawn"):
        assert before[key] == after[key]


def test_an_untouched_sidecar_set_reproduces_the_same_recipe(module) -> None:
    body = {
        "ents": '{"entities": []}',
        "hulls": "0 0 0 0 0 0\n",
        "dispcol": "0 0 0 0 0 0 0 0 0\n",
        "ropes": "rope 0 0 0 1 1 1\n",
    }
    with tempfile.TemporaryDirectory() as out:
        root = Path(out)
        _write_map(root, "sp_test", body)
        first = module.level_sidecar_recipe(root, "sp_test")
        second = module.level_sidecar_recipe(root, "sp_test")
    assert first == second


def test_a_missing_sidecar_digests_to_none_rather_than_failing(module) -> None:
    with tempfile.TemporaryDirectory() as out:
        root = Path(out)
        root.mkdir(parents=True, exist_ok=True)
        # sm_pawnshop_1 in the real export corpus ships no `.dispcol` (no displacements) -- a
        # missing sidecar is a real, recurring shape, not just an edge case.
        _write_map(root, "sm_test", {"ents": "{}", "hulls": "0 0 0 0 0 0\n"})
        recipe = module.level_sidecar_recipe(root, "sm_test")
    sidecars = recipe["runtime_sidecars"]
    assert sidecars["dispcol_sha256"] is None
    assert sidecars["ropes_sha256"] is None
    assert sidecars["ents_sha256"] is not None
    assert sidecars["hulls_sha256"] is not None
