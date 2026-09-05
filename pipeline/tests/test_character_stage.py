"""A GLB-only skeletal stage cannot fall back to the install or binary MDL decoder."""
import json

import pytest

from elysium_pipeline.formats import eskm, install, mdl_skel
from elysium_pipeline.importers.characters import stage_unit
from elysium_pipeline.skeletal_stage.unit import ModelUnit, SkeletalUnitError
from elysium_pipeline.skeletal_stage.source_api import bone_weights
from pipeline.tests.test_model_glb import _export


def test_native_stage_reads_only_glb_and_keeps_all_semantic_domains(tmp_path, monkeypatch):
    source = _export(tmp_path / "units")
    def forbidden(*args, **kwargs):
        raise AssertionError("the GLB stage attempted to read retail source")
    monkeypatch.setattr(install, "read", forbidden)
    monkeypatch.setattr(mdl_skel, "read_anim", forbidden)
    monkeypatch.setattr(mdl_skel, "read_bones", forbidden)
    root = tmp_path / "stage"
    row = stage_unit(source, root)
    blob = (root / row["payload"]).read_bytes()
    assert {b"SKEL", b"MESH", b"MATL"} <= eskm.directory(blob).keys()
    assert row["meshAsset"] == "/ElysiumBaked/Models/scenery/synthetic/SK_prop"
    body = json.loads((root / row["body"]).read_text())
    assert set(body["sourceSemantics"]) == {"mdl", "facial", "procedural", "secondaryMotion", "cloth", "physics"}
    assert len(body["renderVertexMap"][0]["vertices"]) == 3


def test_non_binary_glb_mask_is_refused_not_thresholded(tmp_path):
    unit = ModelUnit(_export(tmp_path / "units"))
    unit._animations_by_base[10] = {"boneWeights": [0.5]}
    with pytest.raises(SkeletalUnitError, match="non-binary"):
        unit.bone_weights(10)


def test_legacy_adapter_also_refuses_fractional_masks():
    import struct
    from types import SimpleNamespace
    data = bytearray(104)
    struct.pack_into("<i", data, 48, 72)
    struct.pack_into("<f", data, 72, .5)
    with pytest.raises(ValueError, match="binary"):
        bone_weights(bytes(data), SimpleNamespace(base=0, label="bad"), [SimpleNamespace(index=0)])


def test_unchanged_stage_reuses_payloads_but_corrupt_body_data_rebuilds(tmp_path, monkeypatch):
    from elysium_pipeline.importers import characters
    source = _export(tmp_path / "units")
    root = tmp_path / "stage"
    first = stage_unit(source, root)
    real_geometry = characters.geometry
    def forbidden(*args, **kwargs):
        raise AssertionError("an unchanged stage rebuilt its mesh")
    monkeypatch.setattr(characters, "geometry", forbidden)
    second = stage_unit(source, root)
    assert second["reused"] and second["recipe"] == first["recipe"]
    monkeypatch.setattr(characters, "geometry", real_geometry)
    (root / first["body"]).write_text('{"lost":true}')
    third = stage_unit(source, root)
    assert not third.get("reused")
    assert "sourceSemantics" in json.loads((root / third["body"]).read_text())
