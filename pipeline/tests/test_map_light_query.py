"""Gameplay illumination conservation and shadow-mask witness for 0005 requirement 1."""
import json
import math

import pytest

from elysium_pipeline import paths
from elysium_pipeline.exporters import UE_map_sidecars as sidecars
from elysium_pipeline.importers import map_light_query as query


def _raw(**changes):
    row = dict(type=1, style=32, cluster=7, origin={"source": [10, 20, 30]},
               normal={"source": [0, 1, 0]}, intensity=[50000, 2000, 1],
               radius={"source": 100}, constantAttn=0, linearAttn=0, quadraticAttn=0,
               exponent=0, stopdot=0.9, stopdot2=0.5)
    row.update(changes)
    return row


def test_light_record_preserves_raw_energy_and_cm_falloff():
    row = query.worldlight_record(_raw(type=2))
    assert list(row["intensity"].values()) == [50000, 2000, 1]
    assert row["position"] == pytest.approx(dict(x=25.4, y=-50.8, z=76.2))
    assert row["normal"] == dict(x=0, y=-1, z=0)
    assert row["radius"] == 254
    assert row["exponent"] == 1
    assert row["quadratic"] * 254**2 == pytest.approx(100**2)
    assert row["style"] == 32 and row["cluster"] == 7
    mixed = query.worldlight_record(_raw(constantAttn=3, linearAttn=4, quadraticAttn=5))
    assert mixed["quadratic"] * 254**2 + mixed["linear"] * 254 + mixed["constant"] == pytest.approx(50403)


def test_quake_reads_linear_distance_not_radius():
    row = query.worldlight_record(_raw(type=4, radius={"source": 900}, linearAttn=40))
    assert row["quakeDistance"] == pytest.approx(101.6)
    assert row["radius"] == 2286


def test_shadow_mask_includes_shadowonly_but_excludes_glass_grate_clip():
    assert 0x18000120 & query.SHADOW_MASK
    assert 0x80 & query.SHADOW_MASK
    assert all((value & query.SHADOW_MASK) == 0 for value in (2, 8, 0x10000, 0x20000))


def test_tutorial_v2_gameplay_light_inputs():
    unit = sidecars.unit_paths("sp_tutorial_1")["root"]
    if not unit.exists():
        pytest.skip("tutorial V2 source unit unavailable")
    join = sidecars.prepare_join("sp_tutorial_1")
    row = query.stage_for_join(join)
    original = join.units.lighting["worldLights"]
    assert len(original) == len(row["records"]["lights"]) == 396
    assert [light["intensity"] for light in row["records"]["lights"]] == [
        dict(zip(("x", "y", "z"), light["intensity"])) for light in original]
    assert len(row["records"]["pvs"]) == row["records"]["numClusters"] * math.ceil(row["records"]["numClusters"] / 8)
    assert len(row["displacements"]) == 3584
    assert row["hulls"] and row["sky"]
    json.loads(json.dumps(row))  # The editor consumes this representation.
    report = {"map": row["map"], "sha256": row["sha256"],
              "worldlights": len(original), "clusters": row["records"]["numClusters"],
              "worldShadowHulls": len(row["hulls"]), "skyTriangles": len(row["sky"]),
              "displacementTriangles": len(row["displacements"]),
              "rawIntensityParity": True}
    report_dir = paths.work_root() / "reports" / "0005-r1"
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / "tutorial-light-query-stage.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
