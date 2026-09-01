"""R3.5 (`docs/project/seam_migration.md` -> "Roadmap -- one pipeline"): the R3.2 producer becomes
the default source of a map's legacy sidecars, and `.weather`/`.particles` generation re-points at
the producer's `.ents` instead of the legacy one `UE_bsp_to_scene.main` already used and discarded.

These pin the orchestration in `elysium_pipeline.exporters.export_all` -- call order and which
`.ents` each downstream sidecar reads -- with the producer, `weather` and `particles` mocked out,
so the test needs no BSP, no VtMB install and no published V2 units. The scoped bake against the
real three-map corpus is the integration witness (`seam_migration.md`'s Settled entry for R3.5).
"""
from __future__ import annotations

import json
from pathlib import Path

from elysium_pipeline.exporters import UE_bsp_to_scene, export_all
from elysium_pipeline.exporters import UE_map_sidecars
from elysium_pipeline.formats import install, particles, weather


def test_rewrite_sidecars_via_producer_calls_the_producer_and_repoints_particles(tmp_path, monkeypatch):
    out_dir = tmp_path
    ents_path = out_dir / "sp_tutorial_1.ents"
    producer_calls = []

    def fake_write_sidecars(name, *, out_dir):
        producer_calls.append((name, out_dir))
        # The real producer writes `.ents` before this call returns; the fake mirrors that so the
        # read-back below sees the producer's document, not a legacy one.
        ents_path.write_text(
            json.dumps({"entities": [{"classname": "worldspawn", "keys": {"producer": "yes"}}]}),
            encoding="utf-8",
        )
        return {"map": name, "ready": True}

    particle_calls = []

    def fake_write_particles(name, out_dir_arg, entity_document, index):
        particle_calls.append((name, out_dir_arg, entity_document, index))
        return None

    weather_calls = []
    monkeypatch.setattr(UE_map_sidecars, "write_sidecars", fake_write_sidecars)
    monkeypatch.setattr(particles, "write_particles", fake_write_particles)
    monkeypatch.setattr(weather, "write_weather", lambda *a, **k: weather_calls.append((a, k)))

    report = export_all.rewrite_sidecars_via_producer(
        "sp_tutorial_1", out_dir, {"idx": True}, legacy_report=None
    )

    assert producer_calls == [("sp_tutorial_1", out_dir)]
    assert report == {"map": "sp_tutorial_1", "ready": True}
    assert len(particle_calls) == 1
    name, out_dir_arg, entity_document, index = particle_calls[0]
    assert (name, out_dir_arg, index) == ("sp_tutorial_1", out_dir, {"idx": True})
    # Read back from the file the producer just wrote, not a copy captured before the overwrite.
    assert entity_document["entities"][0]["keys"]["producer"] == "yes"
    # sp_tutorial_1 carries no weather sidecar; a legacy_report of None must not synthesize one.
    assert weather_calls == []


def test_rewrite_sidecars_via_producer_repoints_weather_when_legacy_report_carries_inputs(
    tmp_path, monkeypatch
):
    out_dir = tmp_path
    ents_path = out_dir / "sm_hub_1.ents"

    def fake_write_sidecars(name, *, out_dir):
        ents_path.write_text(
            json.dumps({"entities": [{"classname": "env_wind", "keys": {}}]}), encoding="utf-8"
        )
        return {"map": name, "ready": True}

    monkeypatch.setattr(UE_map_sidecars, "write_sidecars", fake_write_sidecars)
    monkeypatch.setattr(particles, "write_particles", lambda *a, **k: None)

    weather_calls = []
    monkeypatch.setattr(weather, "write_weather", lambda *a, **k: weather_calls.append(a))

    cover_triangles = [((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0))]
    bounds_min, bounds_max = (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)
    legacy_report = {"weather_inputs": (cover_triangles, bounds_min, bounds_max)}

    export_all.rewrite_sidecars_via_producer("sm_hub_1", out_dir, {}, legacy_report)

    assert len(weather_calls) == 1
    map_name, out_dir_arg, entity_document, _idx, triangles, bmin, bmax = weather_calls[0]
    assert map_name == "sm_hub_1"
    assert out_dir_arg == out_dir
    assert entity_document["entities"][0]["classname"] == "env_wind"
    assert (triangles, bmin, bmax) == (cover_triangles, bounds_min, bounds_max)


def test_export_maps_calls_the_producer_after_the_legacy_export(tmp_path, monkeypatch):
    calls = []

    def fake_main(bsp_path, out_dir, *, index=None):
        calls.append(("main", Path(out_dir)))
        return {"weather_inputs": None}

    def fake_rewrite(name, out_dir, index, legacy_report):
        calls.append(("rewrite", name, out_dir, index, legacy_report))

    monkeypatch.setattr(UE_bsp_to_scene, "main", fake_main)
    monkeypatch.setattr(export_all, "rewrite_sidecars_via_producer", fake_rewrite)
    monkeypatch.setattr(install, "map_path", lambda name: f"/install/{name}.bsp")

    index = {"marker": "shared"}
    results = export_all.export_maps(
        ["sp_tutorial_1"], out_root=tmp_path, index=index, available=["sp_tutorial_1"]
    )

    assert [entry[0] for entry in calls] == ["main", "rewrite"]
    assert calls[1][1:] == (
        "sp_tutorial_1", tmp_path / "sp_tutorial_1", index, {"weather_inputs": None}
    )
    assert results[0].status == "ok"
