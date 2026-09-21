"""The editor half of the decal lane (0018 story 21-4), against a fake `unreal`.

`bake_map_v2._staged_decal` adapts one staged projector row into the `bake_lib.DecalDef` the
shared placer already reads, and `bake_verify.decal_errors` asks the level whether it carries the
rows the stage produced. Both run here with no live editor. The staging half is
`test_map_decals.py`.

The verifier's check is new with this story: before it there was no `verify_decals` at all, and a
level that had lost every projector would have passed every lane the bake ran.
"""
from __future__ import annotations

import importlib.util
import os
import sys
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]


def _load(name):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_" + name, REPO / "pipeline" / "unreal" / (name + ".py"))
    return spec, importlib.util.module_from_spec(spec)


ROWS = [
    {"index": 0, "materialId": "vtmb:material:decals/stains/rusta", "face": 7,
     "locCm": [-4470.4, -238.6416, -483.936],
     "normal": [1.0, 0.0, 0.0], "sDir": [0.0, -1.0, 0.0], "tDir": [0.0, 0.0, -1.0],
     "halfWCm": 20.32, "halfHCm": 81.28},
    {"index": 1, "materialId": "vtmb:material:decals/details/sprayb", "face": 19,
     "locCm": [100.0, 200.0, 300.0],
     "normal": [0.0, 1.0, 0.0], "sDir": [1.0, 0.0, 0.0], "tDir": [0.0, 0.0, -1.0],
     "halfWCm": 64.0, "halfHCm": 32.0},
]
PAYLOAD = {
    "version": 1, "map": "synthetic", "sha256": "abc",
    "counts": {"placed": 2, "unresolved": 0, "unbound": 0, "unmatched": 0, "materials": 2,
               "projectorFaces": 99, "dispFacesSkipped": 3},
    "rows": ROWS,
}


# --------------------------------------------------------------------------- the adapter


def test_a_staged_row_becomes_the_decal_def_the_placer_reads():
    """`_place_decals` is untouched by this story: it still reads `mat`, `loc`, `normal`, `s_dir`,
    `t_dir` and the two half-extents. The row carries the material by its `vtmb:material:` id, the
    identity every other placement lane names a material by, and the bare key is what
    `decal_instance_path` folds."""

    # `bake_map_v2` imports `bake_lib`, which builds asset factories at module scope; the fake
    # only has to answer the calls that happen at import.
    fake = SimpleNamespace(
        Vector=lambda x=0.0, y=0.0, z=0.0: (x, y, z),
        AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
        MaterialEditingLibrary=object(), GeometryScript_Collision=object(),
        EditorAssetLibrary=SimpleNamespace(
            does_directory_exist=lambda target: True, make_directory=lambda target: True,
            does_asset_exist=lambda target: False, load_asset=lambda target: None,
            list_assets=lambda package, recursive=True, include_folder=True: []),
        Paths=SimpleNamespace(project_dir=lambda: str(REPO)),
        SystemLibrary=SimpleNamespace(get_command_line=lambda: ""),
        LinearColor=lambda *values: values,
        log=lambda *a, **k: None, log_error=lambda *a, **k: None, log_warning=lambda *a, **k: None)
    spec, module = _load("bake_map_v2")
    with mock.patch.dict(sys.modules, {"unreal": fake}), \
            mock.patch.dict(os.environ, {"ELYSIUM_WORK_ROOT": str(REPO)}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        spec.loader.exec_module(module)

    decal = module._staged_decal(ROWS[0])
    assert decal.mat == "decals/stains/rusta"
    assert decal.loc == (-4470.4, -238.6416, -483.936)
    assert decal.normal == (1.0, 0.0, 0.0)
    assert decal.s_dir == (0.0, -1.0, 0.0)
    assert decal.t_dir == (0.0, 0.0, -1.0)
    assert decal.half_w == 20.32 and decal.half_h == 81.28


# --------------------------------------------------------------------------- the verifier


def _verify_module():
    """`bake_verify` against the editor surface it touches at import
    (`test_bake_map_ropes.py`'s own loader)."""
    spec, module = _load("bake_verify")
    editor = SimpleNamespace(
        does_directory_exist=lambda target: True, make_directory=lambda target: True,
        does_asset_exist=lambda target: False, load_asset=lambda target: None,
        list_assets=lambda package, recursive=True, include_folder=True: [])
    fake = SimpleNamespace(
        AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
        MaterialEditingLibrary=object(), GeometryScript_Collision=object(),
        EditorAssetLibrary=editor, Paths=SimpleNamespace(project_dir=lambda: str(REPO)),
        SystemLibrary=SimpleNamespace(get_command_line=lambda: ""),
        LinearColor=lambda *values: values,
        log=lambda *a, **k: None, log_error=lambda *a, **k: None, log_warning=lambda *a, **k: None)
    with mock.patch.dict(sys.modules, {"unreal": fake}), \
            mock.patch.dict(os.environ, {"ELYSIUM_WORK_ROOT": str(REPO)}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        with pytest.raises(SystemExit):
            spec.loader.exec_module(module)   # main() refuses: no -BakeMaps= on the command line
    return module


def _placed(verify, rows=None, **overrides):
    out = []
    for row in (ROWS if rows is None else rows):
        facts = {
            "sortOrder": row["index"],
            "material": verify._decal_instance_path(row["materialId"]),
            "locCm": list(row["locCm"]),
            "sizeCm": [verify.DECAL_HALF_DEPTH, row["halfHCm"], row["halfWCm"]],
        }
        facts.update(overrides)
        out.append(facts)
    return out


def test_the_instance_path_is_the_r7_2_fold():
    verify = _verify_module()
    assert verify._decal_instance_path("vtmb:material:decals/stains/rusta") == \
        "/ElysiumBaked/Materials/decals/stains/MI_rusta_Decal"
    # A bare key folds identically: the prefix is stripped when present, never required.
    assert verify._decal_instance_path("decals/stains/rusta") == \
        "/ElysiumBaked/Materials/decals/stains/MI_rusta_Decal"


def test_verify_accepts_a_level_that_matches_its_rows():
    verify = _verify_module()
    assert verify.decal_errors("m", _placed(verify), PAYLOAD) == []


def test_verify_names_a_missing_an_unstaged_and_a_doubled_actor():
    """A level that lost its projectors is exactly what nothing asked about before this story.
    The check keys on the component's sort order, so it survives the editor returning the actors
    in an order that is not the bake's -- the lesson `elysium.src=` taught the rope lane."""
    verify = _verify_module()
    errors = verify.decal_errors("m", _placed(verify, rows=ROWS[:1]), PAYLOAD)
    assert any("have no actor" in error for error in errors)
    assert verify.decal_errors("m", [], PAYLOAD)

    stray = _placed(verify)
    stray.append(dict(_placed(verify, rows=ROWS[:1])[0], sortOrder=99))
    assert any("was not staged" in error for error in verify.decal_errors("m", stray, PAYLOAD))

    twice = _placed(verify) + _placed(verify, rows=ROWS[:1])
    assert any("placed twice" in error for error in verify.decal_errors("m", twice, PAYLOAD))

    # The editor's order is not the bake's, and the check must not care.
    assert verify.decal_errors("m", list(reversed(_placed(verify))), PAYLOAD) == []


def test_verify_catches_a_moved_decal_and_a_rebound_material():
    verify = _verify_module()
    moved = verify.decal_errors("m", _placed(verify, locCm=[0.0, 0.0, 0.0]), PAYLOAD)
    assert any("stands at" in error for error in moved)
    rebound = verify.decal_errors(
        "m", _placed(verify, material="/ElysiumBaked/Materials/MI_other_Decal"), PAYLOAD)
    assert any("binds /ElysiumBaked/Materials/MI_other_Decal" in error for error in rebound)


def test_verify_catches_a_swapped_extent():
    """`decal_size` is `(half depth, half height, half width)`: a deferred decal maps texture U to
    local Z and V to local Y, so a projector whose extents were fed in the wrong order draws the
    poster on its side rather than not at all."""
    verify = _verify_module()
    placed = _placed(verify)
    placed[0]["sizeCm"] = [verify.DECAL_HALF_DEPTH, ROWS[0]["halfWCm"], ROWS[0]["halfHCm"]]
    errors = verify.decal_errors("m", placed, PAYLOAD)
    assert any("half height" in error for error in errors)
    assert any("half width" in error for error in errors)


def test_verify_catches_a_flattened_projection_box():
    verify = _verify_module()
    placed = _placed(verify)
    placed[1]["sizeCm"] = [0.0, ROWS[1]["halfHCm"], ROWS[1]["halfWCm"]]
    assert any("half depth" in error for error in verify.decal_errors("m", placed, PAYLOAD))
