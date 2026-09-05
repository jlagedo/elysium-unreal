"""R5.2 (`docs/project/seam_migration.md` -> "Roadmap -- one pipeline" -> "R5.2 Sky baked"):
the bake-side half of the sky join `pipeline/unreal/bake_map.py::_place_sky` now finishes for a
`MapsOnV2Models` map instead of deferring to the runtime.

`sky_join_intensity` is `ElysiumMapVisuals::SkyAmbientIntensity`'s own C1/C2 policy
(`Source/ElysiumUE/Private/Visual/ElysiumMapVisuals.cpp`), restated here so the bake computes the
same three-case join instead of a fourth implementation that merely claims to agree. `sky_dome_geometry`
is `ElysiumMapVisuals.cpp`'s own `BuildSkyBox`, restated the same way. Both are plain functions with
no `unreal` import, reusing `test_level_sidecar_recipe.py`'s fake-editor-module loader only because
`bake_map.py` itself is an editor entry point that cannot otherwise import.
"""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import re
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
        "elysium_test_bake_map_sky", REPO / "pipeline/unreal/bake_map.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"unreal": _fake_unreal()}), \
            mock.patch.dict(os.environ, {"ELYSIUM_EXPORT_ROOT": export_root}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        try:
            spec.loader.exec_module(module)
        except SystemExit:
            pass
    return module


@pytest.fixture(scope="module")
def module():
    with tempfile.TemporaryDirectory() as out:
        yield _load_bake_map(out)


def test_no_pair_or_zero_pair_is_zero(module) -> None:
    # 83 of 108 maps carry no type-5 row; two of the 25 that do author a zero. Both read as an
    # authored zero, not a missing one -- no divide, no fallback.
    assert module.sky_join_intensity(0.0, 0.7) == 0.0
    assert module.sky_join_intensity(-1.0, 0.7) == 0.0


def test_black_cube_is_zero_not_infinity(module) -> None:
    # A cube whose upper hemisphere integrates to nothing cannot be scaled to a target radiance;
    # the guard is UE's own KINDA_SMALL_NUMBER, not a bespoke epsilon.
    assert module.sky_join_intensity(0.4, 0.0) == 0.0
    assert module.sky_join_intensity(0.4, module.KINDA_SMALL_NUMBER) == 0.0


def test_normal_pair_scales_by_the_cubes_own_mean(module) -> None:
    # The whole of C1: scale the cube so its own average radiance IS the type-5 magnitude.
    assert module.sky_join_intensity(0.8, 0.4) == pytest.approx(2.0)
    assert module.sky_join_intensity(0.1, 0.25) == pytest.approx(0.4)


def _runtime_build_sky_box():
    """Parses `ElysiumMapVisuals.cpp`'s own `BuildSkyBox` -- the `Verts`/`Quads` tables it
    literally contains -- rather than restating a second, hand-copied literal of them, so this
    test actually breaks when the C++ table drifts instead of only when this file's own copy
    does. Signs, not numbers: `Verts` is read as +/-H per axis and compared against
    `sky_dome_geometry`'s the same way, since the C++ side only ever spells the half-extent as
    the parameter `H`.
    """
    src = (REPO / "Source/ElysiumUE/Private/Visual/ElysiumMapVisuals.cpp").read_text(
        encoding="utf-8")
    body_match = re.search(r"void BuildSkyBox\([^)]*\)\s*\{(.*?)\n\t\}", src, re.DOTALL)
    assert body_match, "BuildSkyBox not found in ElysiumMapVisuals.cpp"
    body = body_match.group(1)

    verts_match = re.search(r"Verts\s*=\s*\{(.*?)\};", body, re.DOTALL)
    assert verts_match, "Verts table not found in BuildSkyBox"
    sign = {"-H": -1, "H": 1}
    verts = [
        tuple(sign[term.strip()] for term in vert.split(","))
        for vert in re.findall(r"\{([^{}]*)\}", verts_match.group(1))
    ]

    quads_match = re.search(r"Quads\[6\]\[4\]\s*=\s*\{(.*?)\};", body, re.DOTALL)
    assert quads_match, "Quads table not found in BuildSkyBox"
    quads = [
        tuple(int(index) for index in quad.split(","))
        for quad in re.findall(r"\{([^{}]*)\}", quads_match.group(1))
    ]
    return verts, quads


def test_sky_dome_geometry_matches_the_runtime_box(module) -> None:
    positions, normals, uvs, tris = module.sky_dome_geometry()
    h = module.SKY_DOME_HALF_EXTENT_CM
    runtime_verts, runtime_quads = _runtime_build_sky_box()

    assert len(runtime_verts) == 8
    assert positions == [tuple(sign * h for sign in vert) for vert in runtime_verts]

    expected_tris = []
    for a, b, c, d in runtime_quads:
        expected_tris.extend((a, b, c, a, c, d))
    assert tris == expected_tris

    assert len(normals) == len(uvs) == 8
    assert set(normals) == {(0.0, 0.0, 1.0)}


def test_sky_baked_packages_are_named_by_sky_not_by_map(module) -> None:
    # The game shares six skies between 108 maps; the bake keys by name so two maps that share a
    # sky share one asset set instead of duplicating it.
    assert module.SKY_TEX_PKG == "/ElysiumBaked/Textures/skybox"
    assert module.SKY_MESH_PKG == "/Game/ElysiumGenerated/Sky"
    assert module.SKY_MAT_PKG == "/ElysiumBaked/Materials/skybox"
