"""R5.6 (`docs/architecture/seam_map_map_lighting.md` -> "## Import" -> "Lights final"): the V2
map bake writes every VtMB-derived light value once, from the staged `lights[]` table.

Three things in that section can change shipped content without any other test noticing: the
**rows** (the staged table has to be the `.lights` producer's own rows, fixups included, or the
lump-15 ordinal the calibration asset keys on drifts), the **derivation** (`derive_light` is
`UElysiumLightRig::ApplyToSource` restated in Python, and a transcription slip there is a whole map
lit wrong with every count still matching), and the **manifest version** the editor half refuses
anything but. The corpus case skips, loudly, when the working maps are not on this machine.
"""

from __future__ import annotations

import importlib.util
import math
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
from unittest import mock

import pytest

from elysium_pipeline import paths
from elysium_pipeline.exporters import UE_map_sidecars as sidecars
from elysium_pipeline.importers import map_geometry as MG

REPO = Path(__file__).resolve().parents[2]
WORKING_MAPS = ("sp_tutorial_1", "sm_pawnshop_1", "sm_hub_1")

#: `Config/DefaultElysium.ini`'s lighting-page defaults, as `bake_map_v2.lighting_calibration`
#: reads them off the CDO (every value the R4.3 page carried over unchanged).
CALIBRATION = {
    "PointSpotScale": 0.003, "MaxBrightness": 8.0, "ExtendedMaxBrightness": 512.0,
    "bUseExtendedBrightnessCeiling": False, "FalloffExponent": 1.0, "RadiusScale": 1.0,
    "FallbackRadiusCm": 2500.0, "IndirectLightingScale": 1.0, "VolumetricScatteringScale": 1.0,
    "SunScaleLux": 8.0, "SunSourceAngleDegrees": 0.5357, "SunSoftSourceAngleDegrees": 0.0,
    "MinSkyReachCm": 5000.0, "bPointShadows": True, "bSpotShadows": True, "bSunShadows": True,
    "LightSpecularScale": 1.0,
}


def _fake_unreal():
    """The editor module surface `bake_lib` touches at import (`test_bake_map_captures`)."""
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
        "elysium_test_bake_map_v2_lights", REPO / "pipeline/unreal/bake_map_v2.py")
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


def _row(**overrides):
    row = dict(index=0, type=1, position=[100.0, -200.0, 300.0], direction=[0.0, 0.0, -1.0],
               rgb=[100.0, 50.0, 25.0], radiusCm=1300.48, stopdot=0.0, stopdot2=0.0,
               exponent=1.0, style=0, sky=0)
    row.update(overrides)
    return row


def test_derive_light_restates_apply_to_source(module) -> None:
    # Point: unitless intensity = clamp(max(rgb) * PointSpotScale, ceiling), reach = authored
    # radius, colour normalised by the magnitude, shadows from the page, MegaLights allowed.
    point = module.derive_light(_row(), CALIBRATION)
    assert point["intensity"] == pytest.approx(100.0 * 0.003)
    assert point["reach_cm"] == pytest.approx(1300.48)
    assert point["color"] == pytest.approx([1.0, 0.5, 0.25])
    assert point["cast_shadows"] is True
    assert point["allow_mega_lights"] is True
    assert point["position"] == pytest.approx([100.0, -200.0, 300.0])
    assert point["falloff_exponent"] == 1.0
    assert point["specular_scale"] == 1.0

    # The brightness ceiling clips; the extended ceiling is an A/B that stops clipping.
    hot = module.derive_light(_row(rgb=[9000.0, 9000.0, 9000.0]), CALIBRATION)
    assert hot["intensity"] == 8.0
    extended = module.derive_light(
        _row(rgb=[9000.0, 9000.0, 9000.0]),
        dict(CALIBRATION, bUseExtendedBrightnessCeiling=True))
    assert extended["intensity"] == pytest.approx(27.0)

    # A texlight never shadows; a radius at or under 1 cm is no cutoff and takes the fallback.
    tex = module.derive_light(_row(type=0, radiusCm=0.0), CALIBRATION)
    assert tex["cast_shadows"] is False
    assert tex["reach_cm"] == 2500.0

    # Spot: cones from the stopdot cosines, clamped to 1..80 and inner never past outer.
    spot = module.derive_light(
        _row(type=2, stopdot=0.9396926, stopdot2=0.7660444), CALIBRATION)
    assert spot["outer_cone_deg"] == pytest.approx(40.0, abs=1e-4)
    assert spot["inner_cone_deg"] == pytest.approx(20.0, abs=1e-4)
    wide = module.derive_light(_row(type=2, stopdot=0.5, stopdot2=0.9), CALIBRATION)
    assert wide["inner_cone_deg"] == pytest.approx(wide["outer_cone_deg"])
    assert module.derive_light(_row(type=2, stopdot2=-1.0), CALIBRATION)["outer_cone_deg"] == 80.0

    # Sun: lux off the magnitude with the 0.01 floor, no reach, no MegaLights, angles clamped.
    sun = module.derive_light(_row(type=3, rgb=[1.0, 1.0, 1.0]), CALIBRATION)
    assert sun["intensity"] == pytest.approx(8.0)
    assert sun["reach_cm"] == 0.0
    assert sun["allow_mega_lights"] is False
    assert sun["sun_source_angle_deg"] == pytest.approx(0.5357)
    dim_sun = module.derive_light(_row(type=3, rgb=[1e-6, 1e-6, 1e-6]), CALIBRATION)
    assert dim_sun["intensity"] == 0.01


def test_a_miniature_light_takes_the_sky_transform_and_the_reach_floor(module) -> None:
    # Position `scale * (p - origin)`; reach scaled by the same factor, floored at MinSkyReachCm
    # so a degenerate authored radius still lights something after the scale.
    sky = module.derive_light(
        _row(sky=1, position=[1010.0, 2020.0, 3030.0], radiusCm=200.0),
        CALIBRATION, sky_scale=16.0, sky_origin=(1000.0, 2000.0, 3000.0))
    assert sky["position"] == pytest.approx([160.0, 320.0, 480.0])
    assert sky["reach_cm"] == pytest.approx(5000.0)
    far = module.derive_light(
        _row(sky=1, radiusCm=1000.0), CALIBRATION, sky_scale=16.0, sky_origin=(0.0, 0.0, 0.0))
    assert far["reach_cm"] == pytest.approx(16000.0)


def test_light_rows_apply_the_engines_load_time_fixups() -> None:
    # `Mod_LoadWorldlights`: a spot with exponent 0 lights with 1, a radius under one inch is no
    # cutoff, and the sun/skyambient are never miniature content whatever area they sit in.
    def light(**overrides):
        record = dict(type=1, origin={"source": [10.0, 20.0, 30.0]},
                      normal={"source": [0.0, 0.0, -1.0]}, intensity=[1.0, 2.0, 3.0],
                      exponent=0.0, radius={"source": 0.5}, stopdot=0.1, stopdot2=0.2, style=3)
        record.update(overrides)
        return record

    units = SimpleNamespace(lighting={"worldLights": [
        light(type=2), light(type=3, radius={"source": 100.0}), light(type=5), light(type=1),
    ]})
    sky = SimpleNamespace(is_sky=lambda point: True)
    rows = sidecars.light_rows(units, sky)

    assert [row["index"] for row in rows] == [0, 1, 2, 3]
    assert rows[0]["exponent"] == 1.0 and rows[3]["exponent"] == 0.0
    assert rows[0]["radiusCm"] == 0.0
    assert rows[1]["radiusCm"] == pytest.approx(254.0)
    assert [row["sky"] for row in rows] == [1, 0, 0, 1]
    # The frame is `source_to_unreal`: inches to centimetres with Y negated.
    assert rows[0]["position"] == pytest.approx([25.4, -50.8, 76.2])
    assert rows[0]["direction"] == pytest.approx([0.0, 0.0, -1.0])
    assert rows[0]["style"] == 3
    # The line the sidecar writes for the same row (the negated-zero Y is the legacy exporter's
    # own byte, kept: the R3.3 differ compares these files byte for byte).
    assert sidecars.format_light_line(rows[0]).split() == [
        "2", "25.4000", "-50.8000", "76.2000", "0.0000", "-0.0000", "-1.0000",
        "1.000000", "2.000000", "3.000000", "0.0000", "0.1000", "0.2000", "1.000", "3", "1"]


def test_manifest_version_is_the_offline_stages(module) -> None:
    # The invariant is that the two halves agree, not the number they agree on: the
    # constant is restated across the numpy boundary and bumps whenever a table is added
    # (R7.2 took it to 8), and a literal here only teaches the next bump to edit it here too.
    assert module.MANIFEST_VERSION == MG.MANIFEST_VERSION


@pytest.mark.parametrize("map_name", WORKING_MAPS)
def test_staged_light_rows_format_back_to_the_legacy_lights_sidecar(map_name) -> None:
    unit = MG.sidecars.unit_paths(map_name)["lighting"]
    lights_path = paths.export_root() / map_name / f"{map_name}.lights"
    if not unit.is_file():
        pytest.skip(f"no exported map lighting unit at {unit}")
    if not lights_path.is_file():
        pytest.skip(f"no legacy light sidecar at {lights_path}")

    join = sidecars.prepare_join(map_name)
    rows = sidecars.light_rows(join.units, join.sky)
    lines = lights_path.read_text(encoding="ascii").splitlines()

    # One row per lump-15 record, in lump order, formatting to the very line the sidecar holds:
    # the ordinal a baked actor's `elysium.src` names is the `.lights` line index.
    assert len(rows) == len(join.units.lighting["worldLights"]) == len(lines)
    assert [sidecars.format_light_line(row) for row in rows] == lines
    assert [row["index"] for row in rows] == list(range(len(lines)))
    assert not any(math.isnan(v) for row in rows for v in row["position"])
