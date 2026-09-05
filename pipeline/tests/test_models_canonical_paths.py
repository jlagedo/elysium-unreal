"""R8.0a static-model addressing, publication and shared-root ownership boundaries."""
from copy import deepcopy
import json
from types import SimpleNamespace as NS

import pytest

from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.importers import models
from test_importers_models import _material_index, _model_document
from test_model_import_editor import editor_module  # noqa: F401 -- pytest fixture

ID = "vtmb:model:scenery/furniture/table"
PATH = "/ElysiumBaked/Models/scenery/furniture/SM_table"
MATERIAL = "vtmb:material:models/x/brick"


def staged_entry(key="scenery/furniture/table", document=None):
    return models.stage_unit(key, document or _model_document(key), "a" * 64,
                             material_index=_material_index([MATERIAL]), model_settings=(1., .001, .9))


def manifest():
    entry, _ = staged_entry()
    return {"schemaVersion": models.MANIFEST_SCHEMA, "settingsVersion": models.SETTINGS_VERSION,
            "producer": models.PRODUCER, "packageRoot": models.PACKAGE_ROOT,
            "missingModelAsset": models.MISSING_MODEL_ASSET_PATH, "missingMaterialAsset": models.MI_V2_MISSING,
            "skinCatalogueAsset": models.skin_set_asset_path(), "keep": [models.MISSING_MODEL_ASSET_PATH],
            "selection": {"perMap": None, "keys": ["scenery/furniture/table"]},
            "pruneScope": models.prune_scope(), "assets": [entry], "stageFailures": []}


def test_canonical_addresses_keep_directories_and_do_not_use_legacy_stems():
    assert models.asset_path_for("scenery/furniture/table") == PATH
    assert models.MISSING_MODEL_ASSET_PATH == "/ElysiumBaked/Models/_Corpus/SM_Missing"
    assert models.asset_path_for("scenery/a_b") != models.asset_path_for("scenery/a/b")
    assert models.asset_path_for("scenery/a/table") != models.asset_path_for("scenery/b/table")
    failures = []
    rows = [staged_entry(k)[0] for k in ("scenery/a_b", "scenery/a/b")]
    assert len(models._fold_owner_collisions(rows, lambda *r: failures.append(r))) == 2
    assert not failures
    rows = [staged_entry(k)[0] for k in ("scenery/a-b/table", "scenery/a_b/table")]
    assert models._fold_owner_collisions(rows, lambda *r: failures.append(r)) == []
    assert len(failures) == 2


def test_static_source_cloth_keeps_its_sm_and_unchanged_render_physics_provenance():
    key = ID[len("vtmb:model:"):]
    plain = _model_document(key)
    cloth = deepcopy(plain)
    cloth["extensions"][models.MODEL_EXTENSION]["cloth"] = {"garments": [{"index": 0, "bones": [0]}]}
    entry, provenance = staged_entry(key, cloth)
    baseline, baseline_provenance = staged_entry(key, plain)
    assert entry == baseline and provenance == baseline_provenance
    assert entry["shape"] == "static" and entry["assetPath"] == PATH
    assert baked_unit(ID, "SK") == PATH.replace("/SM_", "/SK_")
    assert entry["collision"]["massKg"] == 12.
    assert entry["slots"] == provenance["slots"]
    assert entry["skinFamilies"] == provenance["skinFamilies"]


def test_stage_refuses_identity_address_mismatch():
    doc = _model_document("scenery/different")
    with pytest.raises(models.ModelImportError, match="disagrees"):
        staged_entry(document=doc)


@pytest.mark.parametrize("reverse", [False, True])
def test_static_material_route_ignores_derived_twins_in_either_order(tmp_path, reverse):
    base = {"unit": MATERIAL, "assetPath": baked_unit(MATERIAL, "MI"),
            "basePropertyOverrides": {"blendMode": "Opaque"}, "parent": "M_V2_Lit"}
    twin = {**base, "assetPath": baked_unit(MATERIAL, "MI", role="Skinned"),
            "basePropertyOverrides": {"blendMode": "Masked"}, "parent": "M_V2_LitSkinned"}
    rows = [base, twin]
    if reverse: rows.reverse()
    (tmp_path / "manifest.json").write_text(json.dumps({"assets": rows}))
    result = models.load_material_index(tmp_path)[MATERIAL]
    assert result == {"assetPath": base["assetPath"], "blendMode": "Opaque", "master": "M_V2_Lit"}
    (tmp_path / "manifest.json").write_text(json.dumps({"assets": [twin]}))
    assert models.load_material_index(tmp_path) == {}


def test_editor_accepts_only_the_canonical_producer_manifest(tmp_path, editor_module):
    doc = manifest()
    path = tmp_path / "manifest.json"
    path.write_text(json.dumps(doc))
    assert editor_module.load_manifest(path) == json.loads(json.dumps(doc))
    assert editor_module.PACKAGE_ROOT == models.PACKAGE_ROOT
    assert editor_module.SETTINGS_VERSION == models.SETTINGS_VERSION
    assert editor_module.SKIN_CATALOGUE_ASSET_PATH == models.skin_set_asset_path()


def test_offline_stage_output_is_accepted_by_editor_loader(tmp_path, editor_module, monkeypatch):
    from test_models_stage_merge import _publish, _stage
    source, stage = tmp_path / "source", tmp_path / "stage"
    _publish(source, "scenery/furniture/table")
    result = _stage(source, stage, monkeypatch, all_models=True)
    loaded = editor_module.load_manifest(result.manifest_path)
    assert not result.failures and loaded["assets"][0]["assetPath"] == PATH
    assert loaded["producer"] == "models"
    assert loaded["skinCatalogueAsset"] == models.skin_set_asset_path()
    assert loaded["assets"][0]["collision"]["massKg"] == 12.


@pytest.mark.parametrize("mutation", ["root", "producer", "settings", "placeholder", "catalogue", "old-entry", "unit-path", "recipe", "scoped-prune", "keep", "duplicate"])
def test_editor_refuses_stale_or_cross_owner_publication(tmp_path, editor_module, mutation):
    doc = manifest()
    if mutation == "root": doc["packageRoot"] = "/ElysiumBaked/Meshes"
    if mutation == "producer": doc["producer"] = "characters"
    if mutation == "settings": doc["settingsVersion"] = "elysium-model-import-v2"
    if mutation == "placeholder": doc["missingModelAsset"] = "/ElysiumBaked/Meshes/SM_elysium_missing_model"
    if mutation == "catalogue": doc["skinCatalogueAsset"] = "/ElysiumBaked/Meshes/DA_ElysiumPropSkins"
    if mutation == "old-entry": doc["assets"][0]["assetPath"] = "/ElysiumBaked/Models/SM_scenery_furniture_table"
    if mutation == "unit-path": doc["assets"][0]["unitGlb"] = "../other.glb"
    if mutation == "recipe": doc["assets"][0]["recipe"]["settingsVersion"] = "elysium-model-import-v2"
    if mutation == "scoped-prune": doc["selection"]["perMap"] = {"test": ["scenery/furniture/table"]}
    if mutation == "keep": doc["keep"] = ["/ElysiumBaked/Shared/Meshes/SM_table"]
    if mutation == "duplicate": doc["assets"].append(deepcopy(doc["assets"][0]))
    path = tmp_path / "manifest.json"
    path.write_text(json.dumps(doc))
    with pytest.raises(editor_module.ManifestError): editor_module.load_manifest(path)


@pytest.mark.parametrize("owner", [None, "characters", "catalogues", "map-bake"])
def test_forced_import_never_replaces_foreign_or_unstamped_assets(editor_module, monkeypatch, owner):
    deleted = []
    monkeypatch.setattr(editor_module, "unreal", NS(EditorAssetLibrary=NS(does_asset_exist=lambda _: True)))
    monkeypatch.setattr(editor_module, "bl", NS(stored_producer=lambda _: owner,
        recipe_fingerprint=lambda *_: "new", delete_owned_asset=lambda p: deleted.append(p)))
    with pytest.raises(RuntimeError, match="cannot replace"):
        editor_module.Tracker(force=True).needs_import(manifest()["assets"][0])
    assert deleted == []


def test_reuse_probe_never_deletes_an_owned_asset_before_geometry_succeeds(editor_module, monkeypatch):
    deleted = []
    monkeypatch.setattr(editor_module, "unreal", NS(EditorAssetLibrary=NS(does_asset_exist=lambda _: True)))
    monkeypatch.setattr(editor_module, "bl", NS(stored_producer=lambda _: "models", stored_recipe=lambda *a, **k: "old",
        recipe_fingerprint=lambda *_: "new", asset_class_name=lambda _: "OtherClass", delete_owned_asset=lambda p: deleted.append(p)))
    assert editor_module.Tracker().needs_import(manifest()["assets"][0])
    assert not deleted


def test_shared_root_prune_retains_skeletal_twins_foreign_and_unstamped(editor_module, monkeypatch):
    # Exercise the shared implementation with a fake registry, without a real editor or asset.
    from pipeline.unreal import bake_lib
    stale = "/ElysiumBaked/Models/scenery/SM_stale"
    skeletal = "/ElysiumBaked/Models/scenery/furniture/SK_table"
    catalogue = models.skin_set_asset_path()
    unstamped = "/ElysiumBaked/Models/scenery/SM_unstamped"
    legacy = "/ElysiumBaked/Meshes/SM_legacy"
    owners = {PATH: "models", stale: "models", skeletal: "characters", catalogue: "catalogues", unstamped: None, legacy: "models"}
    deleted = []
    library = NS(does_directory_exist=lambda _: True,
                 list_assets=lambda root, **kw: [p for p in owners if p.startswith(root + "/")])
    monkeypatch.setattr(bake_lib, "unreal", NS(EditorAssetLibrary=library))
    monkeypatch.setattr(bake_lib, "stored_producer", lambda p: owners[p])
    monkeypatch.setattr(bake_lib, "delete_owned_assets", lambda paths: deleted.extend(paths))
    monkeypatch.setattr(editor_module, "bl", bake_lib)
    counts = {}
    assert editor_module.prune(models.PACKAGE_ROOT, {PATH, catalogue}, models.prune_scope(), counts) == 1
    assert deleted == [stale] and counts == {"foreign": 2, "unstamped": 1}
    with pytest.raises(editor_module.ManifestError): editor_module.prune("/ElysiumBaked/Meshes", [], "/ElysiumBaked/Meshes/")


def test_canonical_static_import_cannot_publish_a_partial_legacy_skin_table(editor_module):
    with pytest.raises(RuntimeError, match="merged static/skeletal"):
        editor_module.author_skin_set(manifest(), None)


@pytest.mark.parametrize("failed", [False, True])
def test_run_reports_global_catalogue_handoff_and_defers_prune_on_failure(tmp_path, editor_module, monkeypatch, failed):
    doc = manifest()
    monkeypatch.setattr(editor_module, "load_manifest", lambda _: doc)
    monkeypatch.setattr(editor_module, "unreal", NS(AssetRegistryHelpers=NS(get_asset_registry=lambda:
        NS(scan_paths_synchronous=lambda *a, **k: None))))
    for name in ("log", "fail"): monkeypatch.setattr(editor_module, name, lambda *a: None)
    monkeypatch.setattr(editor_module, "author_missing_model", lambda *a, **k: None)
    monkeypatch.setattr(editor_module, "MaterialCache", lambda: None)
    monkeypatch.setattr(editor_module, "author_skin_set", lambda *a, **k: pytest.fail("static importer cannot overwrite merged catalogue"))
    def imported(_manifest, _unit, _stage, _tracker, report, _cache):
        if failed: report.failures.append({"reason": "synthetic import failure"})
    monkeypatch.setattr(editor_module, "import_entries", imported)
    prunes = []
    monkeypatch.setattr(editor_module, "prune", lambda root, keep, scope, counts: prunes.append((root, keep, scope)) or 0)
    report = editor_module.run(str(tmp_path / "manifest.json"), str(tmp_path))
    assert prunes[0][2] == (None if failed else models.prune_scope())
    assert models.skin_set_asset_path() in prunes[0][1]
    assert report.as_dict()["pruneDeferred"] == failed
    assert report.as_dict()["catalogueFinalization"]["assetPath"] == models.skin_set_asset_path()
