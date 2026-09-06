"""Packaging reachability contracts without Unreal, registry mutation or producer work."""
from copy import deepcopy
import importlib.util
import json
import os
from pathlib import Path
from types import SimpleNamespace

import pytest

from elysium_pipeline import cook_roots as roots


def declarations(extra=()):
    return {"expectedPackages": sorted([*roots.GLOBALS, *extra]), "inputs": [],
            "sourceDecisions": {"models": {"skipped": [{"unit": "w_null", "reason": "source-only/no geometry"}]}},
            "catalogueManifestSupplied": True}


def published(extra=()):
    result = []
    for path in [*roots.GLOBALS, *extra]:
        result.append({"packagePath": path, "objectPath": roots.object_path(path),
                       "className": roots.GLOBALS.get(path, "AnimSequence"),
                       "producer": "test-producer", "recipe": "recipe"})
    return result


def test_unused_published_assets_are_explicitly_rooted_and_absences_survive():
    unused = "/ElysiumBaked/Models/unused/A_never_selected"
    source = declarations()
    plan = roots.plan_roots(source, published([unused]))
    assert plan["readyToPublish"]
    assert plan["extraPublishedPackages"] == [unused]
    assert unused in [r["packagePath"] for r in plan["targets"]]
    assert json.loads(plan["sourceEvidenceJson"])["sourceDecisions"] == source["sourceDecisions"]
    assert all("w_null" not in r["objectPath"] for r in plan["targets"])
    assert plan["physicsScope"] == "export-import-data-conservation"


def test_missing_simulation_asset_never_gates_r8_but_published_one_is_conserved():
    deferred = "/ElysiumBaked/Models/test/PHYS_later"
    plan = roots.plan_roots(declarations([deferred]), published())
    assert plan["readyToPublish"] and plan["deferredUnpublishedPhysicsPackages"] == [deferred]
    plan = roots.plan_roots(declarations([deferred]), published([deferred]))
    assert plan["readyToPublish"] and not plan["deferredUnpublishedPhysicsPackages"]
    assert deferred in [r["packagePath"] for r in plan["targets"]]


@pytest.mark.parametrize("failure", ["missing", "unstamped", "redirector", "wrong-global", "catalogues"])
def test_missing_or_unverified_publication_cannot_be_blessed(failure):
    rows, source = published(), declarations()
    if failure == "missing": rows.pop()
    elif failure == "unstamped": rows[0]["producer"] = ""
    elif failure == "redirector": rows[0]["className"] = "ObjectRedirector"
    elif failure == "wrong-global": rows[0]["className"] = "DataAsset"
    else: source["catalogueManifestSupplied"] = False
    assert not roots.plan_roots(source, rows)["readyToPublish"]


def test_root_never_manages_itself_and_duplicate_exports_are_rejected():
    source, rows = declarations(), published([roots.ROOT_PACKAGE])
    assert len(roots.plan_roots(source, rows)["targets"]) == len(roots.GLOBALS)
    with pytest.raises(roots.CookRootError, match="duplicate"):
        roots.plan_roots(source, rows + [rows[0]])
    with pytest.raises(roots.CookRootError, match="escaped"):
        roots.plan_roots(source, published(["/ElysiumBaked/Shared/SM_legacy"]))


def test_label_registration_is_specific_and_non_editor_only():
    setting = roots.asset_manager_setting()
    assert 'bIsEditorOnly=False' in setting and 'CookRule=AlwaysCook' in setting
    assert 'Directories=()' in setting and roots.object_path(roots.ROOT_PACKAGE) in setting
    assert 'PrimaryAssetType="ElysiumR8CookRoot"' in setting
    assert 'CookAll' not in setting


def test_map_selection_uses_deployed_delivery_namespace():
    assert roots.cook_map_packages(["vtmb:map:sp_tutorial_1"]) == [
        "/ElysiumBaked/sp_tutorial_1/sp_tutorial_1", "/Game/ElysiumGenerated/Boot"]
    with pytest.raises(roots.CookRootError): roots.cook_map_packages([])


def test_package_object_forms_and_cooked_output_coverage():
    path = "/ElysiumBaked/Models/test/SK_body"
    assert roots.package_path(path + ".SK_body") == path
    with pytest.raises(roots.CookRootError): roots.package_path(path + ".Another")
    plan = roots.plan_roots(declarations(), published())
    assert roots.verify_cooked_packages(plan, roots.GLOBALS)["complete"]
    assert roots.verify_cooked_packages(plan, [*roots.GLOBALS, "/Engine/Fonts/A-Foreign-Font"])["complete"]
    assert not roots.verify_cooked_packages(plan, list(roots.GLOBALS)[:-1])["complete"]


def test_merged_manifest_inventory_is_not_restricted_to_last_slice(tmp_path):
    older = "/ElysiumBaked/Models/old/SK_older"
    paths = {}
    for kind, owner in roots.OWNERS.items():
        document = {"schemaVersion": "1.0.0", "producer": owner, "stageFailures": [], "assets": [],
                    "skipped": [], "summary": {}, "complete": True, "references": [], "expectedInventories": {}}
        if kind == "characters":
            document.update(selectedUnits=["vtmb:model:recent"], assets=[
                {"assetId": "vtmb:model:old", "meshAsset": older}],
                inventory=[{"assetId": "vtmb:model:source_only", "selected": False, "reason": "no consumer"}])
        if kind == "models":
            document["skipped"] = [{"unit": "w_null", "reason": "source-only", "assetPath": "/ElysiumBaked/Models/SM_w_null"}]
        path = tmp_path / (kind + ".json")
        path.write_text(json.dumps(document), encoding="utf-8")
        paths[kind] = path
    source = roots.read_declarations(paths)
    assert older in source["expectedPackages"]
    assert older in source["referencePackages"]
    assert "/ElysiumBaked/Models/SM_w_null" not in source["expectedPackages"]
    assert source["sourceDecisions"]["characters"]["inventory"][0]["reason"] == "no consumer"
    roots.verify_inputs(source)
    paths["characters"].write_text("{}")
    with pytest.raises(roots.CookRootError, match="stale"):
        roots.verify_inputs(source)


def test_producers_cannot_point_back_to_packaging_root(tmp_path):
    paths = {}
    for kind, owner in roots.OWNERS.items():
        value = {"schemaVersion": "1.0.0", "producer": owner, "stageFailures": [], "complete": True,
                 "assets": [], "inventory": [], "selectedUnits": [], "skipped": [], "references": [],
                 "expectedInventories": {}, "summary": {}}
        if kind == "models": value["assets"] = [{"assetPath": roots.ROOT_PACKAGE}]
        path = tmp_path / (kind + ".json"); path.write_text(json.dumps(value)); paths[kind] = path
    with pytest.raises(roots.CookRootError, match="downstream"):
        roots.read_declarations(paths)


@pytest.fixture
def worker():
    path = Path(__file__).parents[1] / "unreal/import_cook_roots.py"
    spec = importlib.util.spec_from_file_location("cook_root_worker_test", path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


def test_registry_snapshot_does_not_load_any_target_object(worker):
    calls = []
    class Registry:
        def scan_paths_synchronous(self, *args, **kwargs): calls.append("scan")
        def get_assets_by_path(self, scope, **kwargs):
            assert kwargs == {"recursive": True, "include_only_on_disk_assets": True}
            calls.append(scope)
            return [SimpleNamespace(package_name=scope+"/DA_example", asset_name="DA_example",
                asset_class_path=SimpleNamespace(asset_name="DataAsset"),
                get_tag_value=lambda name: {"ElysiumProducer": "producer", "ElysiumRecipe": "hash"}[name])]
    unreal = SimpleNamespace(AssetRegistryHelpers=SimpleNamespace(get_asset_registry=Registry))
    assert len(worker.snapshot(unreal)) == 2
    assert calls == ["scan", *roots.SCOPES]


def test_worker_reuse_verifies_label_without_loading_corpus(worker):
    calls = []
    class Label:
        def get_path_name(self): return roots.object_path(roots.ROOT_PACKAGE)
        @staticmethod
        def verify(asset, value): calls.append("verify"); return ""
    label = Label()
    def load(path):
        assert path == roots.ROOT_PACKAGE
        calls.append("load-root"); return label
    unreal = SimpleNamespace(ElysiumCookRoot=Label, load_asset=load,
        EditorAssetLibrary=SimpleNamespace(get_metadata_tag=lambda *args: roots.PRODUCER))
    bl = SimpleNamespace(PRODUCER_TAG="owner", stored_recipe=lambda *args, **kwargs: "recipe")
    asset, state = worker._publish(unreal, bl, roots.plan_roots(declarations(), published()), "recipe")
    assert asset is label and state == "reused" and calls == ["load-root", "verify"]


def test_job_does_not_report_cook_ready_until_rules_pass(worker, monkeypatch, tmp_path):
    packaging = tmp_path / "packaging"; packaging.mkdir()
    source = tmp_path / "producer"; source.mkdir()
    job = packaging / "job.json"
    job.write_text(json.dumps({"manifestPaths": {"models": str(source / "manifest.json")}}))
    monkeypatch.setattr(worker, "publish", lambda *args, **kwargs: {"readyToCook": False, "cookRuleCheck": "unregistered"})
    report = worker.run(job)
    assert report["complete"] is False
    assert json.loads((packaging / "cook_root_import_report.json").read_text())["cookRuleCheck"] == "unregistered"
    inside = source / "job.json"; inside.write_text(job.read_text())
    with pytest.raises(RuntimeError, match="outside producer"):
        worker.run(inside)


@pytest.mark.skipif(not os.environ.get("ELYSIUM_COOK_ROOT_INVENTORY"), reason="explicit read-only manifest/disk inventory")
def test_current_disk_inventory_is_audit_only():
    base = Path(r"E:\elysium-work\import")
    manifests = {"characters": base / "characters/manifest.json", "models": base / "models/manifest.json",
                 "expressions": base / "expression-tables/manifest.json"}
    if (base / "model-catalogues/manifest.json").is_file(): manifests["catalogues"] = base / "model-catalogues/manifest.json"
    source = roots.read_declarations(manifests)
    rows = roots.scan_package_files(Path(r"E:\dev\elysium-unreal\Plugins\ElysiumBaked\Content"))
    source = roots.reconcile_declarations(source, rows)
    plan = roots.plan_roots(source, rows, metadata_verified=False)
    roots.verify_inputs(source)
    assert not plan["readyToPublish"]  # File presence is not a registry/cook result.
    output = Path(r"E:\elysium-work\_r8_explore\agents\physics\cook_roots_inventory.json")
    output.write_text(json.dumps(plan, indent=2), encoding="utf-8")
    print(json.dumps({"counts": plan["counts"], "missing": len(plan["missingPackages"]),
        "extraPublished": len(plan["extraPublishedPackages"]), "catalogues": source["catalogueManifestSupplied"],
        "firstIssues": plan["issues"][:8]}))
