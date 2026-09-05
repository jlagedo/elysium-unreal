"""The editor half of `uv run elysium import models` (R1.4).

Three things this lane cannot see in a screenshot and cannot fix after the fact:

* the `switchPoints -> ScreenSize` mapping, including the `-1.0` shadow-LOD drop and the clamp;
* the coordinate rule -- `(x, z, y) * 100`, `TANGENT.w` negated, winding reversed **once**;
* slot names and legacy provenance stems remain stable while mesh addresses follow the
  canonical `baked_paths.json` fixture. The old `model_names.json` stays intact for legacy
  readers until replacement acceptance; it no longer determines a new static mesh address.

`pipeline/unreal/import_models.py` runs inside an editor, so it is loaded here against a stub
`unreal` module: the transform is plain arithmetic and belongs under test whatever process it
normally runs in.
"""

from __future__ import annotations

import importlib.util
import json
import math
from copy import deepcopy
from pathlib import Path
import struct
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
    """Keep legacy stem/slot evidence while canonical addresses use their own shared fixture."""
    rows = json.loads(FIXTURE.read_text(encoding="utf-8"))
    assert rows, "the golden table is empty"
    for row in rows:
        key = row["unit"][len("vtmb:model:"):]
        assert importer.static_stem(key) == row["stem"], key
        assert row["assetPath"].rsplit("/", 1)[-1] == row["asset"], key
        for slot in row["slots"]:
            base = safe_name(slot["sourceName"])
            # Occurrence 0 keeps `safe_name(sourceName)` verbatim; a later occurrence of the same
            # folded name takes the `_<slot index>` suffix ("Duplicate folded slot names").
            assert slot["slotName"] == base or slot["slotName"].startswith(base + "_"), slot
    canonical = json.loads(FIXTURE.with_name("baked_paths.json").read_text(encoding="utf-8"))
    for row in canonical:
        if row["id"].startswith("vtmb:model:") and row["prefix"] == "SM":
            assert importer.asset_path_for(row["id"][len("vtmb:model:"):]) == row["path"]


@pytest.fixture
def precision_editor(editor_module, monkeypatch):
    """Model source/render storage and the native helper interface without Unreal."""
    calls = {"created": [], "copied": [], "settings": [], "deleted": []}
    state = {"existing": None, "ignoreSettings": False}

    class Properties:
        def __init__(self, **values):
            self.values = values

        def get_editor_property(self, name):
            return self.values[name]

        def set_editor_property(self, name, value):
            self.values[name] = value

    class Mesh:
        def __init__(self, lods, nanite=False):
            self.source_uvs = deepcopy(lods)
            self.nanite = nanite
            self.properties = {}
            self.settings = [Properties(use_full_precision_u_vs=False, use_high_precision_tangent_basis=False,
                recompute_normals=False, recompute_tangents=False, generate_lightmap_u_vs=False,
                remove_degenerates=False, build_scale3d=(1., 1., 1.), use_backwards_compatible_f16_trunc_u_vs=True)
                for _ in lods]
            self.rebuild()

        def rebuild(self):
            self.render_uvs = [[tuple(value if self.settings[i].values["use_full_precision_u_vs"] or abs(value) <= 65504.
                                     else math.copysign(math.inf, value) for value in uv) for uv in lod]
                               for i, lod in enumerate(self.source_uvs)]
            self.render_precision = [deepcopy(settings.values) for settings in self.settings]

        def get_num_lods(self):
            return len(self.settings)

        def set_editor_property(self, name, value):
            self.properties[name] = value

    class NativePrecision:
        @staticmethod
        def verify_precision(mesh, count):
            if mesh is None or count != len(mesh.settings):
                return "wrong source LOD count"
            for source, render in zip(mesh.settings, mesh.render_precision):
                for key in editor_module.STATIC_PRECISION_SETTINGS:
                    if not source.values[key] or not render[key]:
                        return "source or render precision differs"
            return ""

        @staticmethod
        def apply_precision(mesh, count):
            if state["ignoreSettings"]:
                return "native settings did not persist"
            if not NativePrecision.verify_precision(mesh, count):
                return ""
            for index, settings in enumerate(mesh.settings):
                calls["settings"].append(index)
                for key, value in editor_module.STATIC_PRECISION_SETTINGS.items():
                    settings.set_editor_property(key, value)
            mesh.rebuild()
            return NativePrecision.verify_precision(mesh, count)

    def create(lods, path, options):
        calls["created"].append(options)
        state["existing"] = Mesh(lods, options.nanite_settings.get_editor_property("enabled"))
        return state["existing"], "success"

    def copy(mesh, target, options, lod):
        index = lod.get_editor_property("lod_index")
        assert target.settings[index].values["use_full_precision_u_vs"]
        assert target.settings[index].values["use_high_precision_tangent_basis"]
        calls["copied"].append((index, options))
        target.source_uvs[index] = deepcopy(mesh)
        if options.apply_nanite_settings:
            target.nanite = options.new_nanite_settings.get_editor_property("enabled")
        target.rebuild()
        return mesh, "success"

    unreal = types.SimpleNamespace(StaticMesh=Mesh, StaticMaterial=lambda **k: types.SimpleNamespace(**k),
        ElysiumStaticMeshPrecisionLibrary=NativePrecision,
        get_editor_subsystem=lambda _: pytest.fail("precision must not request an editor subsystem"),
        MeshNaniteSettings=Properties, GeometryScriptCreateNewStaticMeshAssetOptions=Properties,
        GeometryScriptCopyMeshToAssetOptions=Properties, GeometryScriptMeshWriteLOD=Properties,
        GeometryScriptGenerateLightmapUVOptions=types.SimpleNamespace(DO_NOT_GENERATE_LIGHTMAP_U_VS="none"),
        GeometryScriptOutcomePins=types.SimpleNamespace(SUCCESS="success"),
        EditorAssetLibrary=types.SimpleNamespace(does_asset_exist=lambda _: state["existing"] is not None,
                                                load_asset=lambda _: state["existing"]),
        GeometryScript_NewAssetUtils=types.SimpleNamespace(create_new_static_mesh_asset_from_mesh_lods=create),
        GeometryScript_AssetUtils=types.SimpleNamespace(copy_mesh_to_static_mesh=copy))
    bl = types.SimpleNamespace(ensure_dir=lambda _: None, stored_producer=lambda _: "models",
        delete_owned_asset=lambda path: calls["deleted"].append(path), asset_class_name=lambda _: "StaticMesh",
        recipe_fingerprint=lambda stage, path, value: json.dumps(value, sort_keys=True))
    monkeypatch.setattr(editor_module, "unreal", unreal)
    monkeypatch.setattr(editor_module, "bl", bl)
    monkeypatch.setattr(editor_module, "static_mesh_editor", lambda: pytest.fail("precision must not use the unsafe subsystem setter"))
    return types.SimpleNamespace(module=editor_module, state=state, calls=calls, Mesh=Mesh, bl=bl)


@pytest.mark.parametrize("reuse,nanite,lod_count", [(reuse, nanite, lods) for reuse in (False, True)
                                                    for nanite in (False, True) for lods in (1, 3)])
def test_static_authoring_keeps_float32_uvs_and_high_tangent_precision_at_every_lod(precision_editor, reuse, nanite, lod_count):
    f = precision_editor
    f32 = lambda value: struct.unpack("<f", struct.pack("<f", value))[0]
    # Actual finding magnitudes: well above float16, still finite and representable as float32.
    lods = [[(f32(-1.097e24), f32(1.e21)), (f32(.25), f32(65504.))] for _ in range(lod_count)]
    if reuse:
        f.state["existing"] = f.Mesh(lods, nanite)
    mesh = f.module.author_static_mesh("/ElysiumBaked/Models/scenery/SM_test", lods, ["material"], ["body"], nanite, True)
    assert mesh.source_uvs == lods and mesh.render_uvs == lods
    assert all(math.isfinite(v) for row in mesh.render_uvs for uv in row for v in uv)
    assert mesh.nanite == nanite and not f.calls["deleted"]
    assert f.calls["settings"] == list(range(lod_count))
    for settings in mesh.settings:
        assert settings.values == {"use_full_precision_u_vs": True, "use_high_precision_tangent_basis": True,
            "recompute_normals": False, "recompute_tangents": False, "generate_lightmap_u_vs": False,
            "remove_degenerates": False, "build_scale3d": (1., 1., 1.), "use_backwards_compatible_f16_trunc_u_vs": True}
    if reuse:
        assert [i for i, _ in f.calls["copied"]] == list(range(lod_count))
        assert all(o.use_original_vertex_order and not o.enable_recompute_normals for _, o in f.calls["copied"])
        assert all(not o.enable_recompute_tangents and o.generate_lightmap_u_vs == "none" for _, o in f.calls["copied"])
    else:
        options = f.calls["created"][0]
        assert options.enable_collision and options.use_original_vertex_order
        assert not options.enable_recompute_normals and not options.enable_recompute_tangents


def test_static_precision_propagates_native_failure(precision_editor):
    f = precision_editor
    f.state["ignoreSettings"] = True
    with pytest.raises(RuntimeError, match="did not persist"):
        f.module.apply_static_precision(f.Mesh([[(0., 0.)]]), 1)


def test_static_precision_works_without_asset_editor_subsystem(precision_editor):
    f = precision_editor
    mesh = f.Mesh([[(1.e24, 0.)]])
    f.module.apply_static_precision(mesh, 1)
    assert f.module.static_precision_matches(mesh, 1)
    assert f.calls["settings"] == [0]


def test_missing_native_helper_requires_build_before_creating_assets(precision_editor, monkeypatch):
    f = precision_editor
    monkeypatch.setattr(f.module.unreal, "ElysiumStaticMeshPrecisionLibrary", None)
    with pytest.raises(RuntimeError, match="rebuild the editor module"):
        f.module.author_static_mesh("/ElysiumBaked/Models/scenery/SM_test", [[(1.e24, 0.)]], [], [], False, True)
    assert not f.calls["created"]


@pytest.mark.parametrize("flag", ["use_full_precision_u_vs", "use_high_precision_tangent_basis"])
def test_recipe_reuse_checks_precision_on_every_lod(precision_editor, flag):
    f = precision_editor
    mesh = f.Mesh([[(1.e24, 0.)], [(1., 0.)]])
    f.module.apply_static_precision(mesh, 2)
    f.state["existing"] = mesh
    entry = {"assetPath": "/ElysiumBaked/Models/scenery/SM_test", "recipe": {"source": "unchanged"},
             "lods": [{"dropped": False}, {"dropped": False}, {"dropped": True}]}
    tracker = f.module.Tracker()
    digest = tracker.fingerprint(entry)
    assert json.loads(digest)["staticBuildSettings"] == f.module.STATIC_PRECISION_SETTINGS
    f.bl.stored_recipe = lambda *a, **k: digest
    assert not tracker.needs_import(entry)
    mesh.settings[1].values[flag] = False
    assert tracker.needs_import(entry)


def test_reuse_checks_built_precision_not_only_source_settings(precision_editor):
    f = precision_editor
    mesh = f.Mesh([[(1.e24, 0.)]])
    f.module.apply_static_precision(mesh, 1)
    mesh.render_precision[0]["use_high_precision_tangent_basis"] = False
    assert not f.module.static_precision_matches(mesh, 1)
    f.module.apply_static_precision(mesh, 1)
    assert f.module.static_precision_matches(mesh, 1)
