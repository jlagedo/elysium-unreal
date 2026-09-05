"""Native-worker orchestration with an in-memory editor; no real Unreal calls."""
from copy import deepcopy
import hashlib
import json
from types import SimpleNamespace as NS

import pytest

from elysium_pipeline.importers.model_catalogues import KINDS, PRODUCER, object_path
from pipeline.unreal import import_model_catalogues as worker
from pipeline.unreal import verify_model_catalogues as verifier
from test_model_catalogues_stage import corpus, stage  # noqa: F401 -- shared synthetic fixture


@pytest.fixture
def native(corpus, monkeypatch):
    manifest = stage(corpus)
    database, recipes = {}, {}
    calls = {"created": [], "saved": [], "pruned": [], "warnings": [], "apply": []}
    controls = {"reject": "", "saveFailure": ""}

    class Asset:
        def __init__(self, path="/Transient/Candidate", owner=""):
            self.path, self.owner, self.payload = path, owner, None

        def get_path_name(self):
            return self.path

    class Catalogue(Asset):
        @classmethod
        def apply_json(cls, asset, encoded):
            calls["apply"].append(asset.path)
            if cls.__name__ == controls["reject"]:
                return None, "native field validation failed"
            asset.payload = json.loads(encoded)
            return asset, ""

        @staticmethod
        def verify(asset, encoded):
            return "" if asset.payload == json.loads(encoded) else "saved fields differ"

    kinds = {name: type(name, (Catalogue,), {}) for name in KINDS.values()}
    kinds.update({name: type(name, (Asset,), {}) for name in (
        "StaticMesh", "SkeletalMesh", "Skeleton", "AnimSequence", "BlendSpace", "ElysiumBodyData", "MaterialInterface")})
    for ref in manifest["references"]:
        database[ref["path"]] = kinds[ref["class"]](ref["path"], ref["producer"])

    def create(name, directory, kind, factory):
        path = object_path(directory + "/" + name)
        calls["created"].append(path)
        database[path] = kind(path)
        return database[path]

    def save(path):
        calls["saved"].append(path)
        return path != controls["saveFailure"]

    def stamp(asset, digest, producer):
        asset.owner = producer
        recipes[asset.path] = digest

    unreal = NS(**kinds, load_asset=lambda path: database.get(object_path(path)), new_object=lambda kind: kind(),
        DataAssetFactory=lambda: NS(set_editor_property=lambda *a: None),
        AssetToolsHelpers=NS(get_asset_tools=lambda: NS(create_asset=create)),
        EditorAssetLibrary=NS(get_metadata_tag=lambda asset, tag: asset.owner),
        AssetRegistryHelpers=NS(get_asset_registry=lambda: NS(scan_paths_synchronous=lambda *a, **k: None)),
        collect_garbage=lambda: None, log_warning=lambda text: calls["warnings"].append(text), log_error=lambda text: None)
    bl = NS(PRODUCER_TAG="ElysiumProducer", ensure_dir=lambda path: None, save=save, stamp_recipe=stamp,
        stored_recipe=lambda path, producer: recipes.get(object_path(path)) if database.get(object_path(path)) and database[object_path(path)].owner == producer else None,
        recipe_fingerprint=lambda producer, path, value: hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest(),
        prune_owned=lambda root, keep, scope, producer, counts: calls["pruned"].append((root, keep, scope, producer)) or counts.update(foreign=4, unstamped=2) or 0)
    monkeypatch.setattr(worker, "_backend", lambda: (unreal, bl))
    monkeypatch.setattr(worker, "tool_fingerprint", lambda *a: "native-tool")
    return NS(roots=corpus, manifest=manifest, path=corpus["stage"] / "manifest.json", db=database,
              recipes=recipes, unreal=unreal, bl=bl, calls=calls, controls=controls)


@pytest.mark.parametrize("command", [
    '-ImportModelCatalogues="E:/space here/manifest.json" -ImportForce=1',
    '"-ImportModelCatalogues=E:/space here/manifest.json" -ImportForce=1',
])
def test_worker_cli_arguments(command):
    assert worker.argument(command, "ImportModelCatalogues") == "E:/space here/manifest.json"
    assert worker.argument(command, "ImportForce") == "1"
    assert worker.argument(command, "Missing") == ""


def test_worker_publishes_all_three_then_fresh_verifies_all_fields_and_refs(native):
    report = worker.run(native.path)
    assert report["complete"] and report["imported"] == 3
    assert len(native.calls["saved"]) == 3 and len(native.calls["pruned"]) == 1
    root, keep, scope, owner = native.calls["pruned"][0]
    assert root == "/ElysiumBaked/Models/_Corpus" and scope == root + "/" and owner == PRODUCER
    assert keep == set(native.manifest["keep"])
    saved = list(native.calls["saved"])
    checked = verifier.run(native.path)
    assert checked["complete"] and checked["verified"] == 3
    assert checked["references"] == len(native.manifest["references"])
    assert checked["hardReferences"] > 0 and checked["softReferences"] > 0
    assert native.calls["saved"] == saved  # verifier is read-only


def test_current_recipe_does_not_hide_altered_native_fields(native):
    assert worker.run(native.path)["complete"]
    before = len(native.calls["saved"])
    assert worker.run(native.path)["reused"] == 3
    assert len(native.calls["saved"]) == before
    asset = native.db[object_path(native.manifest["assets"][0]["assetPath"])]
    asset.payload["data"]["models"].clear()
    assert not verifier.run(native.path)["complete"]
    assert len(native.calls["saved"]) == before
    repaired = worker.run(native.path)
    assert repaired["complete"] and repaired["imported"] == 1 and repaired["reused"] == 2
    assert native.calls["warnings"]


@pytest.mark.parametrize("problem", ["missing", "class", "owner", "redirect"])
def test_invalid_native_reference_blocks_every_catalogue_write(native, problem):
    ref = native.manifest["references"][0]
    asset = native.db[ref["path"]]
    if problem == "missing": native.db.pop(ref["path"])
    if problem == "class": native.db[ref["path"]] = object()
    if problem == "owner": asset.owner = "another-producer"
    if problem == "redirect": asset.path = "/ElysiumBaked/Legacy/SM_other.SM_other"
    report = worker.run(native.path, force=True)
    assert not report["complete"] and report["pruneDeferred"]
    assert not native.calls["created"] and not native.calls["saved"] and not native.calls["pruned"]


@pytest.mark.parametrize("owner", ["", "characters", "models"])
def test_wrong_target_owner_is_never_overwritten(native, owner):
    entry = native.manifest["assets"][1]
    path = object_path(entry["assetPath"])
    native.db[path] = getattr(native.unreal, entry["nativeClass"])(path, owner)
    assert not worker.run(native.path, force=True)["complete"]
    assert not native.calls["created"] and not native.calls["saved"]


def test_native_field_preflight_happens_for_all_three_before_first_creation(native):
    native.controls["reject"] = KINDS["PropSkins"]
    assert not worker.run(native.path)["complete"]
    assert not native.calls["created"] and not native.calls["saved"] and not native.calls["pruned"]


def test_save_failure_never_reports_complete_or_prunes(native):
    native.controls["saveFailure"] = native.manifest["assets"][1]["assetPath"]
    report = worker.run(native.path)
    assert not report["complete"] and report["pruneDeferred"] and report["failed"]
    assert not native.calls["pruned"]


def test_changed_dll_recipe_requires_reimport_and_fresh_verifier_never_repairs(native, monkeypatch):
    assert worker.run(native.path)["complete"]
    before = len(native.calls["saved"])
    monkeypatch.setattr(worker, "tool_fingerprint", lambda *a: "different-native-tool")
    assert not verifier.run(native.path)["complete"]
    assert len(native.calls["saved"]) == before


def test_stale_input_after_preflight_blocks_publication(native, monkeypatch):
    original = worker.preflight
    def changed(*args):
        result = original(*args)
        path = native.roots["characters"] / "manifest.json"
        path.write_bytes(path.read_bytes() + b" ")
        return result
    monkeypatch.setattr(worker, "preflight", changed)
    report = worker.run(native.path)
    assert not report["complete"]
    assert not native.calls["created"] and not native.calls["saved"] and not native.calls["pruned"]


def test_even_valid_stage_generation_swap_during_preflight_is_refused(native, monkeypatch):
    original = worker.preflight
    def changed(*args):
        result = original(*args)
        manifest = json.loads(native.path.read_bytes())
        manifest["generationNote"] = "different valid manifest"
        native.path.write_text(json.dumps(manifest))
        return result
    monkeypatch.setattr(worker, "preflight", changed)
    report = worker.run(native.path)
    assert not report["complete"]
    assert "generation changed" in report["failed"][0]["reason"]
    assert not native.calls["created"] and not native.calls["saved"] and not native.calls["pruned"]
