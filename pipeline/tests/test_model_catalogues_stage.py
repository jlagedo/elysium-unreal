"""Synthetic GLB/manifest handoff tests; no install, editor or shared output roots."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path

import pytest

from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.formats.unit_contract.container import decode_glb, encode_glb
from elysium_pipeline.importers import model_catalogues as catalogues
from elysium_pipeline.importers.catalogue_common import object_path


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    raw = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode()
    path.write_bytes(raw)
    return hashlib.sha256(raw).hexdigest()


@pytest.fixture
def corpus(tmp_path):
    from pipeline.tests.test_model_glb import _export
    from elysium_pipeline.importers import characters, models, materials
    from elysium_pipeline.skeletal_stage.unit import ModelUnit
    from elysium_pipeline.skeletal_stage import wield

    roots = {name: tmp_path / name for name in ("exports", "characters", "models", "materials")}
    template = _export(tmp_path / "synthetic-template")
    document, binary = decode_glb(template.read_bytes(), str(template))
    ids = ("vtmb:model:character/body", "vtmb:model:weapons/tool")
    docs = {}
    for id in ids:
        doc = deepcopy(document)
        u = doc["extensions"]["ELYSIUM_vtmb_model"]
        u["identity"].update(asset=id, modelPath="models/" + id[11:] + ".mdl",
                             family="character" if id == ids[0] else "weapons",
                             roles=["character-body"] if id == ids[0] else ["placed-prop", "wield"])
        p = roots["exports"] / ("models/" + id[11:] + ".glb")
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(encode_glb(doc, binary))
        docs[id] = doc
    material = docs[ids[0]]["extensions"]["ELYSIUM_vtmb_model"]["materialBindings"]["skinFamilies"][0][0]
    material_path = baked_unit(material, "MI")
    source_material = roots["exports"] / ("materials/" + material[14:] + ".glb")
    source_material.parent.mkdir(parents=True, exist_ok=True)
    source_material.write_bytes(encode_glb({"asset": {"version": "2.0"}, "extensions": {
        "ELYSIUM_vtmb_material": {"identity": {"asset": material}}}}))
    provenance = material[14:] + ".provenance.json"
    provenance_sha = write_json(roots["materials"] / provenance, {"assetId": material, "skinnedAsset": material_path,
                                                                 "unitSha256": catalogues.digest_file(source_material)})
    material_entry = {"unit": material, "assetPath": material_path, "unitGlb": "materials/" + material[14:] + ".glb",
        "unitSha256": catalogues.digest_file(source_material), "provenance": provenance,
        "recipe": {"provenanceSha256": provenance_sha}}
    write_json(roots["materials"] / "manifest.json", {"schemaVersion": "1.0.0", "packageRoot": "/ElysiumBaked/Materials",
        "settingsVersion": materials.SETTINGS_VERSION, "stageFailures": [], "assets": [material_entry]})

    entity = {"index": 3, "classname": "prop_dynamic", "model": {"kind": "model", "asset": ids[1], "path": "models/weapons/tool.mdl"},
        "keyValues": [{"key": "model", "value": "models/weapons/tool.mdl"}], "outputs": [],
        "references": [{"role": "model", "asset": ids[1], "resolved": True}]}
    map_dir = roots["exports"] / "maps"
    map_dir.mkdir(parents=True)
    (map_dir / "test.entities.glb").write_bytes(encode_glb({"asset": {"version": "2.0"}, "extensions": {
        "ELYSIUM_vtmb_map_entities": {"identity": {"asset": "vtmb:map-entities:test"}, "entities": [entity]}}}))
    (map_dir / "test.glb").write_bytes(encode_glb({"asset": {"version": "2.0"}, "extensions": {
        "ELYSIUM_vtmb_map": {"identity": {"asset": "vtmb:map:test"}, "staticProps": {"props": [], "dictionary": []}}}}))
    fields = {key: {"value": value} for key, value in {
        "animPrefix": "", "showsViewModel": True, "cameraClass": "", "bitFlagCantBeLast": False,
        "bitFlagDisciplineTgt": False, "reloadSingle": False}.items()}
    fields["models"] = {"wieldmodel_" + sex: {"raw": "models/weapons/tool.mdl", "asset": ids[1], "resolved": True} for sex in ("f", "m")}
    item = roots["exports"] / "vdata/items/item_tool.glb"
    item.parent.mkdir(parents=True)
    item.write_bytes(encode_glb({"asset": {"version": "2.0"}, "extensions": {"ELYSIUM_vtmb_vdata": {
        "identity": {"asset": "vtmb:vdata:items/item_tool"}, "projection": {"rootKey": "WeaponData", "fields": fields}}}}))
    trees = {ids[0]: {"root": ""}}
    char_entries, static_entries = [], []
    for id, doc in docs.items():
        source = roots["exports"] / ("models/" + id[11:] + ".glb")
        e = characters.stage_unit(source, roots["characters"], wield_bodies=trees if id == ids[1] else None)
        # Minimal already-projected native metadata fixtures: source has no sequence declarations.
        assert ModelUnit(source).sequences == []
        e["meshData"] = id[11:] + ".mesh.json"
        e["recipe"]["meshDataSha256"] = write_json(roots["characters"] / e["meshData"], {"assetId": id})
        e["clipData"] = id[11:] + ".clips.json"
        e["clipDataSha256"] = write_json(roots["characters"] / e["clipData"], {
            "assetId": id, "ownerRoot": "", "clips": {}, "blendSpaces": {}})
        e["bodyData"] = id[11:] + ".vocabulary.json"
        e["bodyDataAsset"] = baked_unit(id, "DA")
        e["bodyDataSha256"] = write_json(roots["characters"] / e["bodyData"], {
            "schemaVersion": "1.0.0", "assetId": id, "ownerRoot": "", "assetPath": e["bodyDataAsset"],
            "sequences": [], "nativeSequences": {}, "nativeBlendSpaces": {}})
        char_entries.append(e)
        s, p = models.stage_unit(id[11:], doc, catalogues.digest_file(source), material_index={material: {
            "assetPath": material_path, "blendMode": "Opaque", "master": "M_V2_Lit"}}, model_settings=(1., .001, .9))
        s["recipe"]["provenanceSha256"] = write_json(roots["models"] / (id[11:] + ".provenance.json"), p)
        static_entries.append(s)
    char_manifest = {"schemaVersion": "1.0.0", "producer": "characters", "packageRoot": "/ElysiumBaked/Models",
        "stageFailures": [], "assets": char_entries, "inventory": [{"assetId": id} for id in ids],
        "wieldCatalogue": wield.discover(roots["exports"], docs)}
    write_json(roots["characters"] / "manifest.json", char_manifest)
    write_json(roots["models"] / "manifest.json", {"schemaVersion": "1.0.0", "producer": "models", "packageRoot": "/ElysiumBaked/Models",
        "settingsVersion": models.SETTINGS_VERSION, "stageFailures": [], "skipped": [], "assets": static_entries})
    roots["stage"] = tmp_path / "catalogues"
    return roots


def stage(roots):
    return catalogues.stage_model_catalogues(roots["exports"], roots["stage"], characters_root=roots["characters"],
                                            models_root=roots["models"], materials_root=roots["materials"])


def test_composes_all_three_complete_catalogues_and_hashes_every_input(corpus):
    result = stage(corpus)
    checked, projections = catalogues.verify_model_catalogue_stage(corpus["stage"] / "manifest.json")
    assert result == checked and len(projections) == 3
    by_kind = {p["catalogueKind"]: p for p in projections}
    assert len(by_kind["PropSkins"]["data"]["models"]) == 2
    assert len(by_kind["PlacedModels"]["data"]["models"]) == 1
    assert len(by_kind["WieldModels"]["data"]["items"]) == 1
    assert all(len(r["representations"]) == 2 for r in by_kind["PropSkins"]["data"]["models"].values())
    assert any(r["class"] == "MaterialInterface" and r["hard"] for r in result["references"])
    assert any(r["class"] == "ElysiumBodyData" for r in result["references"])
    assert result["keep"] == sorted(p["assetPath"] for p in projections)


@pytest.mark.parametrize("mutation", ["static-root", "static-hole", "character-hole", "body-data", "source-digest", "sidecar-digest", "missing-digest", "wield-gap", "material-hole"])
def test_bad_input_preserves_prior_publication(corpus, mutation):
    stage(corpus)
    path = corpus["stage"] / "manifest.json"
    before = path.read_bytes()
    key = "models" if mutation.startswith("static") else "materials" if mutation == "material-hole" else "characters"
    mpath = corpus[key] / "manifest.json"
    m = json.loads(mpath.read_bytes())
    if mutation == "static-root": m["packageRoot"] = "/ElysiumBaked/Meshes"
    if mutation in ("static-hole", "character-hole", "material-hole"): m["assets"].pop()
    if mutation == "body-data": m["assets"][0].pop("bodyData")
    if mutation == "source-digest": m["assets"][0]["recipe"]["unitSha256"] = "0" * 64
    if mutation == "sidecar-digest": m["assets"][0]["recipe"]["bodySha256"] = "0" * 64
    if mutation == "missing-digest": m["assets"][0]["recipe"]["bodySha256"] = None
    if mutation == "wield-gap": m["wieldCatalogue"]["items"] = []
    write_json(mpath, m)
    with pytest.raises((ValueError, KeyError)):
        stage(corpus)
    assert path.read_bytes() == before


def test_stage_verifier_detects_changed_source_new_inventory_and_projected_omission(corpus):
    m = stage(corpus)
    manifest = corpus["stage"] / "manifest.json"
    source = corpus["exports"] / "maps/new.glb"
    source.write_bytes(b"new source")
    with pytest.raises(ValueError, match="inventory changed"):
        catalogues.verify_model_catalogue_stage(manifest)
    source.unlink()  # Only this synthetic test file, within tmp_path.
    row = next(r for r in m["assets"] if r["catalogueKind"] == "PropSkins")
    p = corpus["stage"] / row["projection"]
    data = json.loads(p.read_bytes())
    data["data"]["models"].pop(next(iter(data["data"]["models"])))
    row["projectionSha256"] = row["recipe"]["projectionSha256"] = write_json(p, data)
    write_json(manifest, m)
    with pytest.raises(ValueError, match="coverage"):
        catalogues.verify_model_catalogue_stage(manifest)


def test_output_root_cannot_overlap_any_source_root(corpus):
    corpus["stage"] = corpus["models"] / "catalogues"
    with pytest.raises(ValueError, match="disjoint"):
        stage(corpus)


def test_projection_path_cannot_escape_the_stage(corpus):
    m = stage(corpus)
    m["assets"][0]["projection"] = "../foreign.json"
    write_json(corpus["stage"] / "manifest.json", m)
    with pytest.raises(ValueError, match="escapes"):
        catalogues.verify_model_catalogue_stage(corpus["stage"] / "manifest.json")


def test_composition_cannot_fall_back_to_install_or_legacy_decoders(corpus, monkeypatch):
    from elysium_pipeline.formats import install, mdl_skel
    def forbidden(*args, **kwargs):
        pytest.fail("catalogue composition attempted a non-GLB source read")
    monkeypatch.setattr(install, "read", forbidden)
    monkeypatch.setattr(mdl_skel, "read_anim", forbidden)
    monkeypatch.setattr(mdl_skel, "read_bones", forbidden)
    assert stage(corpus)["complete"]
