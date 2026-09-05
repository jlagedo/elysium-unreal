"""The editor half of `uv run elysium import models` (R1.4).

Three things this lane cannot see in a screenshot and cannot fix after the fact:

* the `switchPoints -> ScreenSize` mapping, including the `-1.0` shadow-LOD drop and the clamp;
* the coordinate rule -- `(x, z, y) * 100`, `TANGENT.w` negated, winding reversed **once**;
* the names. `SM_<safe_name(static_stem(path))>` and `safe_name(sourceName)` per slot are what
  `ApplyPropSkin`, `BindMapMaterials` and the four substrate call sites resolve by, so a renamed
  slot silently unbinds a skin swap rather than failing. The golden table lives in
  `fixtures/model_names.json` and is read from both sides -- here, and by the C++ twin
  (`Elysium.Substrate.ModelNames.PropModelStem`, `ElysiumModelNamesTests.cpp`).

`pipeline/unreal/import_models.py` runs inside an editor, so it is loaded here against a stub
`unreal` module: the transform is plain arithmetic and belongs under test whatever process it
normally runs in.
"""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import types

import pytest

from elysium_pipeline.asset_names import safe_name
from elysium_pipeline.importers import models as importer

FIXTURE = Path(__file__).with_name("fixtures") / "model_names.json"


# --- LOD screen sizes ---------------------------------------------------------------------------


def _lods(*switch_points: float) -> list[dict]:
    return [{"index": index, "mesh": index, "switchPoints": [value], "primitiveCount": 1}
            for index, value in enumerate(switch_points)]


def test_the_switch_point_to_screen_size_mapping():
    """`ScreenSize[0] = 1.0`, then `clamp(constant / switchPoint, floor, ceiling)`, with the
    `-1.0` shadow-LOD drop and the strictly-decreasing correction.

    The order is reciprocal because a coarser LOD is drawn *further away*: the Source metric grows
    with distance while Unreal's `ScreenSize` shrinks with it. The ceiling is what stops a switch
    point below the constant (LOD 1 at `0.5` here) from producing a screen size above the ceiling,
    and the floor what stops a huge one from producing zero.
    """
    rows, anomalies = importer._build_lods(
        _lods(0.0, 0.5, 15.0, 5000.0), constant=1.0, floor=0.001, ceiling=0.9)
    assert [row["screenSize"] for row in rows] == [
        pytest.approx(1.0), pytest.approx(0.9), pytest.approx(1.0 / 15.0), pytest.approx(0.001),
    ]
    assert anomalies == []

    # A `switchPoints` of `-1.0` is a shadow LOD: the LOD is not built, it keeps its own source
    # index in the record, and the built chain closes up behind it.
    rows, _anomalies = importer._build_lods(
        _lods(0.0, 15.0, -1.0), constant=1.0, floor=0.001, ceiling=0.9)
    assert [row["dropped"] for row in rows] == [False, False, True]
    assert rows[2]["screenSize"] is None
    active = [row for row in rows if not row["dropped"]]
    assert [row["screenSize"] for row in active] == [pytest.approx(1.0), pytest.approx(1.0 / 15.0)]

    # The result must be strictly decreasing. Nothing in today's corpus fires this; it exists so
    # that the day it does, it is a report line rather than a silently reordered LOD chain.
    rows, anomalies = importer._build_lods(
        _lods(0.0, 15.0, 15.0), constant=1.0, floor=0.001, ceiling=0.9)
    assert [row["kind"] for row in anomalies] == ["lodScreenSizeNotMonotone"]
    assert rows[2]["screenSize"] == pytest.approx(rows[1]["screenSize"] * 0.5)


# --- the coordinate rule ------------------------------------------------------------------------


class _Stub(types.ModuleType):
    """Enough of `unreal` for `import_models` to import: every attribute answers a callable that
    answers another stub. Nothing under test touches the editor -- the transform is arithmetic."""

    def __getattr__(self, name):  # noqa: D105
        return _Stub(name)

    def __call__(self, *args, **kwargs):
        # The empty string: the only stubbed call the import path actually consumes is
        # `SystemLibrary.get_command_line()`, and an empty command line makes `main()` refuse the
        # run (a `SystemExit` the loader below expects) instead of importing an editor.
        return ""


@pytest.fixture(scope="module")
def editor_module():
    path = Path(__file__).resolve().parents[1] / "unreal" / "import_models.py"
    saved = sys.modules.get("unreal")
    sys.modules["unreal"] = _Stub("unreal")
    try:
        spec = importlib.util.spec_from_file_location("elysium_import_models_under_test", path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        try:
            spec.loader.exec_module(module)
        except SystemExit:
            # `main()` runs at module scope (the house pattern for an editor script) and refuses
            # a run with no manifest; everything above it is already bound.
            pass
        return module
    finally:
        if saved is None:
            del sys.modules["unreal"]
        else:
            sys.modules["unreal"] = saved


def test_the_coordinate_rule_on_a_hand_computed_triangle(editor_module):
    """One triangle, worked out by hand.

    glTF metres Y-up right-handed -> Unreal centimetres Z-up left-handed is `(x, z, y) * 100` for a
    position and the same axis map unscaled for a direction. The map is a reflection (determinant
    -1), which is why the winding reverses **once** and why `TANGENT.w` flips: `B = w * (N x T)`,
    and a reflection negates the cross product. Reversing twice, or forgetting the `w` flip, is
    invisible in a screenshot of a two-sided surface.
    """
    positions = editor_module.unreal_positions([(1.0, 2.0, 3.0), (-0.5, 0.0, 0.25)])
    assert positions == [(100.0, 300.0, 200.0), (-50.0, 25.0, 0.0)]

    # A direction is not scaled, and it is re-normalized.
    normals = editor_module.unreal_directions([(0.0, 1.0, 0.0), (0.0, 0.0, 2.0)])
    assert normals == [(0.0, 0.0, 1.0), (0.0, 1.0, 0.0)]

    # glTF: N = +Y, T = +X, w = +1 -> B = +1 * (N x T) = (0,0,1) x ... in glTF terms.
    # Unreal: N = (0,0,1), T = (1,0,0), w = -1 -> B = -1 * (N x T) = -(0,1,0) = (0,-1,0).
    tangents_x, tangents_y = editor_module.unreal_tangents(
        [(1.0, 0.0, 0.0, 1.0)], [(0.0, 0.0, 1.0)])
    assert tangents_x == [(1.0, 0.0, 0.0)]
    assert tangents_y[0] == pytest.approx((0.0, -1.0, 0.0))
    # And the sign really is the flip: the same tangent with w = -1 gives the opposite bitangent.
    _x, flipped = editor_module.unreal_tangents([(1.0, 0.0, 0.0, -1.0)], [(0.0, 0.0, 1.0)])
    assert flipped[0] == pytest.approx((0.0, 1.0, 0.0))

    # Winding reverses exactly once, at section build: (a, b, c) -> (a, c, b). Stated as the
    # property rather than as a face-normal comparison: reversing is an involution, so "once" is
    # exactly "not the source order, and the source order again if it happened twice" -- which is
    # the error this guards against (the map is a single reflection, so one reversal, not two).
    assert editor_module.reversed_winding([0, 1, 2, 3, 4, 5]) == [(0, 2, 1), (3, 5, 4)]
    once = editor_module.reversed_winding([0, 1, 2])
    assert once != [(0, 1, 2)]
    twice = editor_module.reversed_winding([index for triangle in once for index in triangle])
    assert twice == [(0, 1, 2)]


def test_editor_mesh_projection_preserves_precise_source_positions(editor_module):
    from pipeline.tests.test_model_glb import _unit
    from elysium_pipeline.exporters import model_glb
    _, unit = _unit()
    primitive = unit.lods[0]["primitives"][0]
    x = 480123.987654321
    primitive["positions"][0] = (x, 2., 3.)
    primitive["sourceNormals"][0] = (0., 0., 2.)
    primitive["normals"][0] = (0., 0., 1.)
    document, binary = model_glb.build_document(unit)
    raw = document["meshes"][0]["primitives"]
    sections = editor_module.decode_lod_sections(document, binary, raw, {0: 0}, "precise")
    assert sections[0]["positions"][0] == (x * 2.54, -2. * 2.54, 3. * 2.54)
    assert sections[0]["normals"][0] == (0., 0., 1.)
    rounded = editor_module.unreal_positions(editor_module.accessor(
        document, binary, raw[0]["attributes"]["POSITION"]))[0]
    assert abs(rounded[0] - sections[0]["positions"][0][0]) > 1e-4


def test_non_manifold_sections_are_detected_before_they_are_appended(editor_module):
    """`FDynamicMesh3` refuses a triangle that would make the mesh non-manifold, and a refusal is
    a dropped face plus a log line. Every one of its three refusal conditions is predicted here,
    because the split has to happen *before* the append -- the engine logs the error whatever the
    caller does, and a commandlet that logs an engine error exits non-zero."""

    assert not editor_module.needs_split([(0, 1, 2), (1, 2, 3)])
    assert editor_module.needs_split([(0, 1, 1)])                       # degenerate
    assert editor_module.needs_split([(0, 1, 2), (0, 1, 3), (1, 0, 4)])  # edge with three faces
    assert editor_module.needs_split([(0, 1, 2), (0, 1, 2)])            # duplicate triangle


# --- names --------------------------------------------------------------------------------------


def test_asset_and_slot_names_match_the_golden_table():
    """The Python side of the twin. `FElysiumContentPaths::PropModelStem` runs over the same rows
    in `Elysium.Substrate.ModelNames.PropModelStem`; neither may change without the other failing.

    `safe_name` and `baked_asset_name` are not interchangeable here, and the two folds visibly
    disagree in this table: `cliff-danger` keeps its dash in the stem (`formats.mdl.sanitize`
    keeps `.` and `-`) and loses it in the asset name (`safe_name` keeps only `[A-Za-z0-9_]`).
    """
    rows = json.loads(FIXTURE.read_text(encoding="utf-8"))
    assert rows, "the golden table is empty"
    for row in rows:
        key = row["unit"][len("vtmb:model:"):]
        assert importer.static_stem(key) == row["stem"], key
        assert importer.asset_path_for(key) == row["assetPath"], key
        assert row["assetPath"].rsplit("/", 1)[-1] == row["asset"], key
        for slot in row["slots"]:
            base = safe_name(slot["sourceName"])
            # Occurrence 0 keeps `safe_name(sourceName)` verbatim; a later occurrence of the same
            # folded name takes the `_<slot index>` suffix ("Duplicate folded slot names").
            assert slot["slotName"] == base or slot["slotName"].startswith(base + "_"), slot
