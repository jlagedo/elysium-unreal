"""The per-map entity asset's offline stage (R4.1).

`pipeline/src/elysium_pipeline/importers/map_entities.py` turns the R3.2 producer's entity join
into the manifest `pipeline/unreal/import_map_entities.py` executes, and refuses to write one whose
rows do not match the `<map>.ents` document the asset replaces
(`docs/architecture/seam_map_map_entities.md` -> "Import").

These pin the pieces that decide whether a wrong asset can be authored at all: the parity
comparison itself, the refusal it drives, the asset path that must stay the twin of
`FElysiumContentPaths::BakedMapEntities`, and the extraction that keeps `.ents` and the asset
reading one join.
"""
from __future__ import annotations

import json
from pathlib import Path
from types import SimpleNamespace

import pytest

from elysium_pipeline.exporters import UE_map_sidecars as producer
from elysium_pipeline.importers import map_entities

REPO_ROOT = Path(__file__).resolve().parents[2]


def _row(**overrides):
    row = {
        "classname": "logic_auto",
        "targetname": "boot",
        "origin": [1.0, 2.0, 3.0],
        "start_hidden": False,
        "keys": {"origin": "1 2 3"},
    }
    row.update(overrides)
    return row


def _join():
    """A stand-in for `prepare_join`'s result: the four members `build_entities` is handed."""
    return SimpleNamespace(units=SimpleNamespace(name="sp_probe"), sky=None, pair_blocks=[],
                           brush_meshes={})


def _write_ents(path: Path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="ascii") as handle:
        json.dump({"map": path.stem, "entities": rows}, handle, separators=(",", ":"))


def test_compare_rows_names_the_index_and_the_field_that_differ():
    staged = [_row(), _row(classname="func_door", origin=[4.0, 5.0, 6.0])]
    sidecar = [_row(), _row(classname="func_door", origin=[4.0, 5.0, 6.5])]

    verdict = map_entities.compare_rows(staged, sidecar)

    assert verdict["defCount"] == {"staged": 2, "sidecar": 2, "equal": True}
    assert verdict["mismatchCount"] == 1
    assert verdict["equal"] is False
    assert verdict["indexMismatches"] == [
        {"index": 1, "field": "origin", "staged": [4.0, 5.0, 6.0], "sidecar": [4.0, 5.0, 6.5]}
    ]


def test_compare_rows_catches_a_field_present_on_only_one_side_and_a_count_gap():
    # A row that gained `sky` and a row the sidecar does not have at all: index parity is by lump
    # ordinal, so a count gap is reported as such and never absorbed by matching on classname.
    staged = [_row(sky=True), _row(classname="func_door")]
    sidecar = [_row()]

    verdict = map_entities.compare_rows(staged, sidecar)

    assert verdict["defCount"] == {"staged": 2, "sidecar": 1, "equal": False}
    assert verdict["equal"] is False
    assert verdict["indexMismatches"] == [
        {"index": 0, "field": "sky", "staged": True, "sidecar": None}
    ]


def test_stage_map_refuses_rows_that_do_not_match_the_sidecar(monkeypatch, tmp_path):
    monkeypatch.setattr(producer, "prepare_join", lambda name, root=None: _join())
    monkeypatch.setattr(
        map_entities.producer, "build_entities",
        lambda *args, **kwargs: ([_row(classname="func_door")], {"entities": 1}),
    )
    ents = tmp_path / "sp_probe.ents"
    _write_ents(ents, [_row()])

    with pytest.raises(map_entities.MapEntityStageError) as error:
        map_entities.stage_map("sp_probe", ents_path=ents)

    assert "classname" in str(error.value)


def test_stage_map_entities_writes_a_manifest_carrying_the_rows_and_a_passing_parity(
    monkeypatch, tmp_path
):
    rows = [_row(), _row(classname="func_door", model=18, hulls=[[1.0, 2.0, 3.0]])]
    monkeypatch.setattr(producer, "prepare_join", lambda name, root=None: _join())
    monkeypatch.setattr(
        map_entities.producer, "build_entities",
        lambda *args, **kwargs: (rows, {"entities": len(rows)}),
    )
    ents = tmp_path / "exports" / "sp_probe" / "sp_probe.ents"
    _write_ents(ents, rows)

    staged = map_entities.stage_map_entities(
        tmp_path / "exports_v2", tmp_path / "staging",
        maps=["sp_probe"], ents_for=lambda stem: ents,
    )

    assert staged.maps == ["sp_probe"]
    assert staged.failures == []
    manifest = json.loads(staged.manifest_path.read_text(encoding="utf-8"))
    assert manifest["schemaVersion"] == map_entities.MANIFEST_SCHEMA
    assert manifest["mount"] == "/ElysiumBaked"
    entry = manifest["maps"][0]
    assert entry["assetPath"] == "/ElysiumBaked/Maps/sp_probe/DA_sp_probe_Entities"
    assert entry["packageRoot"] == "/ElysiumBaked/Maps/sp_probe"
    assert entry["entities"] == rows
    assert entry["parity"]["checked"] is True and entry["parity"]["equal"] is True


def test_stage_map_entities_refuses_an_unscoped_run(tmp_path):
    with pytest.raises(map_entities.MapEntityStageError):
        map_entities.stage_map_entities(tmp_path, tmp_path / "staging", maps=[])


def test_the_asset_path_is_the_twin_of_the_cpp_accessor():
    # `FElysiumContentPaths::BakedMapEntities` composes `/ElysiumBaked/<map>/DA_<map>_Entities`; a
    # rename on either side that is not made on the other silently un-cuts every map over.
    assert map_entities.asset_path("sm_hub_1") == "/ElysiumBaked/Maps/sm_hub_1/DA_sm_hub_1_Entities"
    header = (REPO_ROOT / "Source/ElysiumUE/Private/ElysiumContentPaths.h").read_text(
        encoding="utf-8")
    assert 'const FString Asset = TEXT("DA_") + Map + TEXT("_Entities");' in header
    assert "return BakedMapDir(Map) / Asset + TEXT(\".\") + Asset;" in header


def test_write_entities_only_writes_what_build_entities_returned(monkeypatch, tmp_path):
    # The asset stage and the `.ents` file must read one join, so the writer may hold no row
    # logic of its own: it serializes `build_entities`' output and nothing else.
    rows = [_row(), _row(classname="func_door")]
    monkeypatch.setattr(
        producer, "build_entities", lambda *args, **kwargs: (rows, {"entities": len(rows)})
    )

    class _Units:
        name = "sp_probe"

    stats = producer.write_entities(_Units(), None, [], {}, tmp_path)

    assert stats == {"entities": 2}
    written = (tmp_path / "sp_probe.ents").read_text(encoding="ascii")
    assert written == json.dumps({"map": "sp_probe", "entities": rows}, separators=(",", ":"))
