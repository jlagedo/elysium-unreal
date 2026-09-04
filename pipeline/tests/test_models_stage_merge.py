"""Contract tests for the `import models` scoped-run manifest merge (the defect: a `--maps` run
replaced `manifest.json` wholesale, though `_prune_stale` never runs for a scoped selection --
the sidecars a wider run staged survived on disk while the manifest naming them did not).

Mirrors `test_materials_stage.py`'s own `_publish`/`_unit` fixture shape, adapted to model units;
`_model_document`/`_material_index` are `test_importers_models.py`'s own synthetic-fixture
helpers, imported rather than re-authored, the way `test_model_provenance_keys.py` already does.
Real map/entities units and a real staged materials manifest are a different seam's fixture --
this seam is the manifest-merge rule alone, so `select_for_maps` and `load_material_index` are
stubbed to a fixed key set and a fixed material index for every test here.
"""

from __future__ import annotations

import json
from pathlib import Path

from elysium_pipeline.formats.unit_contract.container import encode_glb
from elysium_pipeline.importers import models as importer

from test_importers_models import _material_index, _model_document

MATERIAL_ID = "vtmb:material:models/x/brick"


def _publish(export_v2_root: Path, key: str, **document_kwargs) -> Path:
    document = _model_document(key, **document_kwargs)
    destination = importer.unit_path_for(export_v2_root, key)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(encode_glb(document))
    return destination


def _stub_select_for_maps(map_stems):
    """`select_for_maps`'s real form resolves through real map/entities units and the item-ground
    table (`test_map_scoped_selection_matches_real_corpus` already covers that resolution); this
    stub stands in for it so a scoped run here names exactly the keys the test asks for."""

    def _stub(_export_v2_root, stems, export_root_path=None):
        keys = sorted(set(stems if stems else map_stems))
        return {"perMap": {"stub": keys}, "itemGround": [], "keys": keys}

    return _stub


def _stage(export_v2_root, staging_root, monkeypatch, *, maps=None, all_models=False):
    monkeypatch.setattr(importer, "load_material_index", lambda root: _material_index([MATERIAL_ID]))
    if maps is not None:
        monkeypatch.setattr(importer, "select_for_maps", _stub_select_for_maps(maps))
    return importer.stage_models(export_v2_root, staging_root, maps=maps, all_models=all_models)


def _manifest(result) -> dict:
    return json.loads(result.manifest_path.read_text(encoding="utf-8"))


def test_scoped_run_after_wide_run_keeps_the_wide_runs_rows(tmp_path, monkeypatch):
    export_v2_root = tmp_path / "export_v2"
    staging_root = tmp_path / "stage"
    _publish(export_v2_root, "scenery/a")
    _publish(export_v2_root, "scenery/b")

    wide = _stage(export_v2_root, staging_root, monkeypatch, all_models=True)
    assert wide.staged == 2

    scoped = _stage(export_v2_root, staging_root, monkeypatch, maps=["scenery/a"])
    assert scoped.staged == 1

    manifest = _manifest(scoped)
    stems = {entry["stem"] for entry in manifest["assets"]}
    assert stems == {importer.static_stem("scenery/a"), importer.static_stem("scenery/b")}
    # Unchanged rules the fix does not touch: a scoped run's own selection still names only what
    # it staged, and it still never earns a prune scope.
    assert manifest["selection"]["keys"] == ["scenery/a"]
    assert manifest["pruneScope"] is None

    # The sidecar the wide run wrote for `scenery/b` is still on disk -- a scoped run never
    # prunes -- and the merged manifest is the one description that still matches it.
    assert importer._sidecar_path(staging_root, "scenery/b").is_file()


def test_a_restaged_unit_replaces_its_prior_row(tmp_path, monkeypatch):
    export_v2_root = tmp_path / "export_v2"
    staging_root = tmp_path / "stage"
    _publish(export_v2_root, "scenery/a")
    _publish(export_v2_root, "scenery/b")

    wide = _stage(export_v2_root, staging_root, monkeypatch, all_models=True)
    stale_sha256 = next(
        entry["unitSha256"] for entry in _manifest(wide)["assets"]
        if entry["stem"] == importer.static_stem("scenery/a")
    )

    # Re-publish `scenery/a` with different physics -- a different unit, a different SHA-256 --
    # and re-stage only it.
    _publish(export_v2_root, "scenery/a", physics={
        "solids": [{"properties": {"mass": 99.0, "surfaceprop": "wood"}, "hulls": [{"sourceOffset": 0}]}],
    })
    scoped = _stage(export_v2_root, staging_root, monkeypatch, maps=["scenery/a"])
    assert scoped.staged == 1

    manifest = _manifest(scoped)
    a_rows = [
        entry for entry in manifest["assets"]
        if entry["stem"] == importer.static_stem("scenery/a")
    ]
    assert len(a_rows) == 1, "a re-staged unit must own exactly one row, not a duplicate"
    assert a_rows[0]["unitSha256"] != stale_sha256
    # `scenery/b`, never touched by the scoped run, is still the wide run's own row.
    assert any(
        entry["stem"] == importer.static_stem("scenery/b") for entry in manifest["assets"]
    )


def test_schema_mismatch_falls_back_to_wholesale_replacement(tmp_path, monkeypatch):
    export_v2_root = tmp_path / "export_v2"
    staging_root = tmp_path / "stage"
    _publish(export_v2_root, "scenery/a")
    _publish(export_v2_root, "scenery/b")

    wide = _stage(export_v2_root, staging_root, monkeypatch, all_models=True)
    manifest_path = wide.manifest_path
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    document["schemaVersion"] = "0.0.1-stale"
    manifest_path.write_text(json.dumps(document), encoding="utf-8")

    scoped = _stage(export_v2_root, staging_root, monkeypatch, maps=["scenery/a"])
    manifest = _manifest(scoped)
    stems = {entry["stem"] for entry in manifest["assets"]}
    # No merge material trusted from a manifest whose schema no longer matches this run's --
    # `scenery/b`'s row is gone, the same outright replacement an absent manifest gets.
    assert stems == {importer.static_stem("scenery/a")}
