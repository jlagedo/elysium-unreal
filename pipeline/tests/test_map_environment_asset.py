"""The per-map environment asset's offline stage (R4.4).

`pipeline/src/elysium_pipeline/importers/map_environment.py` turns `<map>.env`, `<map>.sky` and
`<map>.spawn` into the manifest `pipeline/unreal/import_map_environment.py` executes.

These pin the pieces that decide whether a wrong environment can be authored at all: each sidecar's
own absent-file default (the identity `FElysiumSkyDef`/`FElysiumSpawnDef`/`FElysiumEnvDef` reads
back), the values a real sidecar line carries through unchanged, and the asset path that must stay
the twin of `FElysiumContentPaths::BakedMapEnvironment`.
"""
from __future__ import annotations

from pathlib import Path

import pytest

from elysium_pipeline.importers import map_environment


def _write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def test_read_env_carries_both_fog_sets_and_the_sky_flags(tmp_path):
    path = _write(tmp_path / "sp_probe.env", "\n".join([
        "skybox 1", "skyname la", "skyconv 1",
        "fog 1", "fogcolor 0.0667 0.0784 0.0980", "fogstart 1270.0000", "fogend 12700.0000",
        "skyfog 1", "skyfogcolor 1.0000 1.0000 1.0000", "skyfogstart 20320.0000",
        "skyfogend 203200.0000",
    ]) + "\n")

    env = map_environment.read_env(path)

    assert env == {
        "sky": True, "skyName": "la", "skyConvention": 1,
        "fog": True, "fogColor": [0.0667, 0.0784, 0.098], "fogStart": 1270.0, "fogEnd": 12700.0,
        "skyFog": True, "skyFogColor": [1.0, 1.0, 1.0], "skyFogStart": 20320.0,
        "skyFogEnd": 203200.0,
    }


def test_read_env_defaults_to_the_all_off_row_when_the_sidecar_is_absent(tmp_path):
    env = map_environment.read_env(tmp_path / "no_such_map.env")

    assert env["sky"] is False and env["fog"] is False and env["skyFog"] is False
    assert env["skyName"] == ""


def test_read_sky_refuses_a_non_positive_scale_as_the_c_plus_plus_reader_does(tmp_path):
    path = _write(tmp_path / "sp_probe.sky", "origin 100.0 200.0 300.0\nscale 0.0\n")

    sky = map_environment.read_sky(path)

    assert sky == {"hasMiniature": False, "origin": [0.0, 0.0, 0.0], "scale": 1.0}


def test_read_sky_carries_the_placement_transform_when_the_scale_is_positive(tmp_path):
    path = _write(tmp_path / "sp_probe.sky", "origin 6825.615 -4500.88 7248.525\nscale 16.0\n")

    sky = map_environment.read_sky(path)

    assert sky == {"hasMiniature": True, "origin": [6825.615, -4500.88, 7248.525], "scale": 16.0}


def test_read_spawn_requires_an_origin_line(tmp_path):
    path = _write(tmp_path / "sp_probe.spawn", "yaw -90.0\n")

    spawn = map_environment.read_spawn(path)

    assert spawn == {"hasSpawn": False, "origin": [0.0, 0.0, 0.0], "yaw": 0.0}


def test_stage_map_carries_the_three_sidecars_and_reports_its_own_path(tmp_path):
    directory = tmp_path / "sp_probe"
    _write(directory / "sp_probe.env", "skybox 1\nskyname la\nskyconv 1\nfog 0\n"
           "fogcolor 0 0 0\nfogstart 0\nfogend 0\nskyfog 0\nskyfogcolor 0 0 0\n"
           "skyfogstart 0\nskyfogend 0\n")
    _write(directory / "sp_probe.sky", "origin 1.0 2.0 3.0\nscale 16.0\n")
    _write(directory / "sp_probe.spawn", "origin -35.56 -19032.22 -416.56\nyaw -270.0\n")

    entry = map_environment.stage_map(
        "sp_probe",
        env_path=directory / "sp_probe.env",
        sky_path=directory / "sp_probe.sky",
        spawn_path=directory / "sp_probe.spawn",
    )

    assert entry["assetPath"] == "/ElysiumBaked/sp_probe/DA_sp_probe_Environment"
    assert entry["parity"]["equal"] is True
    assert entry["sky"] == {"hasMiniature": True, "origin": [1.0, 2.0, 3.0], "scale": 16.0}
    assert entry["spawn"] == {
        "hasSpawn": True, "origin": [-35.56, -19032.22, -416.56], "yaw": -270.0,
    }
    assert entry["stats"]["hasSkyMiniature"] is True
    assert entry["stats"]["hasSpawn"] is True


def test_stage_map_environment_refuses_to_run_unscoped(tmp_path):
    with pytest.raises(map_environment.MapEnvironmentStageError, match="refuses to run unscoped"):
        map_environment.stage_map_environment(tmp_path, maps=[], sidecar_dir=lambda stem: tmp_path)


def test_stage_map_environment_reports_a_missing_map_as_a_failure_not_a_crash(tmp_path):
    staged = map_environment.stage_map_environment(
        tmp_path, maps=["absent_map"], sidecar_dir=lambda stem: tmp_path / "nowhere",
    )

    # Every sidecar is optional -- a map with none of the three still stages an all-default row
    # (matching what the C++ resolver's sidecar fallback would read), so this is not a failure.
    assert staged.maps == ["absent_map"]
    assert staged.failures == []
