"""`pipeline/unreal/import_surface_properties.py`, the editor half of
`uv run elysium import surface-properties`, exercised against a faked editor.

The script is an editor entry point, so the module is loaded with a fake `unreal` whose asset
registry, factory, physical-material library and asset tools record what the script asked of them.
The fake keeps one dict of assets keyed by package path; stamping through `set_metadata_tag` and
reading back through `bake_lib.stored_recipe` both go through it, so reuse is proved end to end
rather than by patching the decision.
"""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]
ROOT = "/ElysiumBaked/SurfaceProperties"


# --- the fake editor ----------------------------------------------------------------------------


class FakeAsset:
    def __init__(self, path, class_name):
        self.path = path
        self.class_name = class_name
        self.metadata = {}
        self.sidecar = None

    def get_class(self):
        return SimpleNamespace(get_name=lambda: self.class_name)

    def get_path_name(self):
        return "%s.%s" % (self.path, self.path.rsplit("/", 1)[-1])


class FakeEditor:
    """One registry of assets plus the calls the script makes against it."""

    def __init__(self):
        self.assets = {}                 # package path -> FakeAsset
        self.created = []                # (name, package, class name)
        self.deleted = []
        self.saved = []
        self.made_dirs = []
        self.logs, self.warnings, self.errors = [], [], []
        self.scanned = []
        self.command_line = ""
        #: What `create_asset` yields for a name; the class it builds, or None to refuse.
        self.creates_class = {}
        #: Names whose `apply_json` refuses, with the reason.
        self.apply_refuses = {}
        self.save_refuses = set()

    # -- asset tools --
    def create_asset(self, name, package, asset_class, factory):
        built = self.creates_class.get(name, asset_class.name)
        self.created.append((name, package, built))
        if built is None:
            return None
        asset = FakeAsset("%s/%s" % (package, name), built)
        self.assets[asset.path] = asset
        return asset

    # -- EditorAssetLibrary --
    def does_directory_exist(self, target):
        return True

    def make_directory(self, target):
        self.made_dirs.append(target)
        return True

    def does_asset_exist(self, target):
        return target in self.assets

    def load_asset(self, target):
        return self.assets.get(target)

    def list_assets(self, package, recursive=True, include_folder=False):
        prefix = package.rstrip("/") + "/"
        return [asset.get_path_name() for path, asset in self.assets.items()
                if path.startswith(prefix)]

    def get_metadata_tag(self, asset, tag):
        return asset.metadata.get(tag, "")

    def set_metadata_tag(self, asset, tag, value):
        asset.metadata[tag] = value

    def save_asset(self, target, only_if_is_dirty=True):
        if target in self.save_refuses:
            return False
        self.saved.append(target)
        return True

    def delete_asset(self, target):
        self.deleted.append(target)
        return self.assets.pop(target, None) is not None

    def delete_loaded_assets(self, loaded):
        for asset in loaded:
            self.deleted.append(asset.path)
            self.assets.pop(asset.path, None)
        return True

    def delete_directory(self, target):
        return True

    def find_asset_data(self, target):
        asset = self.assets.get(target)
        name = asset.class_name if asset else ""
        return SimpleNamespace(asset_class_path=SimpleNamespace(asset_name=name))

    # -- registry --
    def get_asset_by_object_path(self, object_path):
        asset = self.assets.get(object_path.split(".", 1)[0])
        if asset is None:
            return None
        return SimpleNamespace(
            is_valid=lambda: True,
            get_tag_value=lambda tag, a=asset: a.metadata.get(tag, ""))

    def scan_paths_synchronous(self, paths, force_rescan=False):
        self.scanned = list(paths)


def _fake_unreal(editor):
    def apply_json(material, text):
        name = material.path.rsplit("/", 1)[-1]
        if name in editor.apply_refuses:
            return False, editor.apply_refuses[name]
        try:
            material.sidecar = json.loads(text)
        except ValueError as exc:
            return False, "bad json: %s" % exc
        return True, ""

    def stamp_registry_tags(material):
        if material.sidecar is None:
            return False, "material carries no ElysiumSurfacePropertyProvenance"
        material.metadata["ElysiumAssetId"] = material.sidecar.get("assetId", "")
        material.metadata["ElysiumGameMaterial"] = material.sidecar.get("gameMaterial", "")
        material.metadata["ElysiumSourceName"] = material.sidecar.get("sourceName", "")
        return True, ""

    library = SimpleNamespace(
        does_directory_exist=editor.does_directory_exist,
        make_directory=editor.make_directory,
        does_asset_exist=editor.does_asset_exist,
        load_asset=editor.load_asset,
        list_assets=editor.list_assets,
        get_metadata_tag=editor.get_metadata_tag,
        set_metadata_tag=editor.set_metadata_tag,
        save_asset=editor.save_asset,
        delete_asset=editor.delete_asset,
        delete_loaded_assets=editor.delete_loaded_assets,
        delete_directory=editor.delete_directory,
        find_asset_data=editor.find_asset_data,
    )
    registry = SimpleNamespace(
        get_asset_by_object_path=editor.get_asset_by_object_path,
        scan_paths_synchronous=editor.scan_paths_synchronous,
    )
    return SimpleNamespace(
        AssetToolsHelpers=SimpleNamespace(
            get_asset_tools=lambda: SimpleNamespace(create_asset=editor.create_asset)),
        MaterialEditingLibrary=object(),
        GeometryScript_Collision=object(),
        EditorAssetLibrary=library,
        AssetRegistryHelpers=SimpleNamespace(get_asset_registry=lambda: registry),
        SystemLibrary=SimpleNamespace(
            get_command_line=lambda: editor.command_line, collect_garbage=lambda: None),
        # The class object the script names in create_asset; only its name matters to the fake.
        ElysiumPhysicalMaterial=SimpleNamespace(
            name="ElysiumPhysicalMaterial", apply_json=apply_json,
            stamp_registry_tags=stamp_registry_tags),
        PhysicalMaterialFactoryNew=lambda: SimpleNamespace(),
        load_asset=editor.load_asset,
        log=editor.logs.append,
        log_warning=editor.warnings.append,
        log_error=editor.errors.append,
    )


def _load(editor):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_import_surface_properties",
        REPO / "pipeline/unreal/import_surface_properties.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    fake = _fake_unreal(editor)
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        try:
            spec.loader.exec_module(module)   # main() exits at once: no -ImportSurfaceProperties=
        except SystemExit:
            pass
    # bake_lib was bound to the fake at import; keep it that way for the module's lifetime.
    module.unreal = fake
    return module


# --- staging fixtures ---------------------------------------------------------------------------


def _entry(key, **over):
    entry = {
        "assetPath": "%s/PM_%s" % (ROOT, key),
        "class": "ElysiumPhysicalMaterial",
        "unit": "vtmb:surface-property:%s" % key,
        "unitKey": key,
        "unitGlb": "surface-properties/%s.glb" % key,
        "unitSha256": "sha-" + key,
        "provenance": "%s.provenance.json" % key,
        "surfaceType": "SurfaceType7",
        "gameMaterial": "M",
        "baseChain": [],
        "recipe": {"settingsVersion": "elysium-surfaceproperty-import-v1",
                   "unitSha256": "sha-" + key, "chainSha256": "chain-" + key,
                   "surfaceType": "SurfaceType7"},
    }
    entry.update(over)
    return entry


def _stage(tmp_path, entries, keep=()):
    """Write a manifest plus the sidecar each entry names."""
    for entry in entries:
        (tmp_path / entry["provenance"]).write_text(json.dumps({
            "assetId": entry["unit"],
            "sourceName": entry["unitKey"],
            "gameMaterial": entry["gameMaterial"],
            "surfaceType": entry["surfaceType"],
            "baseChain": entry["baseChain"],
        }), encoding="utf-8")
    manifest = {"schemaVersion": "1.0.0",
                "settingsVersion": "elysium-surfaceproperty-import-v1",
                "packageRoot": ROOT, "select": None, "pruneScope": ROOT + "/",
                "keep": list(keep), "stageFailures": [], "assets": entries}
    path = tmp_path / "manifest.json"
    path.write_text(json.dumps(manifest), encoding="utf-8")
    return str(path)


def _report(tmp_path):
    return json.loads((tmp_path / "import_report.json").read_text(encoding="utf-8"))


# --- the run -------------------------------------------------------------------------------------


def test_a_fresh_run_creates_applies_stamps_and_saves_every_entry(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    entries = [_entry("metal"), _entry("canister", baseChain=["metal"]),
               _entry("weapon", surfaceType="SurfaceType_Default", gameMaterial="",
                      recipe={"settingsVersion": "elysium-surfaceproperty-import-v1",
                              "unitSha256": "sha-weapon", "chainSha256": "chain-weapon",
                              "surfaceType": "SurfaceType_Default"})]
    manifest = _stage(tmp_path, entries)

    report = module.run(manifest, force=False)

    assert report.failures == []
    assert (report.built, report.reused, report.pruned) == (3, 0, 0)
    assert editor.scanned == [ROOT]
    # Every asset is created as the Elysium class through the stock physical-material factory.
    assert [(name, built) for name, _, built in editor.created] == [
        ("PM_metal", "ElysiumPhysicalMaterial"),
        ("PM_canister", "ElysiumPhysicalMaterial"),
        ("PM_weapon", "ElysiumPhysicalMaterial"),
    ]
    assert all(package == ROOT for _, package, _ in editor.created)

    metal = editor.assets[ROOT + "/PM_metal"]
    assert metal.sidecar["assetId"] == "vtmb:surface-property:metal"
    assert metal.metadata["ElysiumGameMaterial"] == "M"
    assert metal.metadata["ElysiumAssetId"] == "vtmb:surface-property:metal"
    assert metal.metadata["ElysiumRecipe"] == module.bl.recipe_fingerprint(
        "surface-properties", entries[0]["assetPath"], entries[0]["recipe"])
    assert sorted(editor.saved) == sorted(entry["assetPath"] for entry in entries)

    written = _report(tmp_path)
    assert written["imported"] == 3 and written["failed"] == []
    assert written["surfaceTypes"] == {"SurfaceType7": 2, "SurfaceType_Default": 1}


def test_a_second_run_reuses_every_stamped_asset(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path, [_entry("metal"), _entry("wood")])
    module.run(manifest)
    editor.created.clear()

    second = module.run(manifest)

    assert second.failures == []
    assert (second.built, second.reused) == (0, 2)
    assert editor.created == []


def test_force_re_authors_a_current_asset(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path, [_entry("metal")])
    module.run(manifest)
    editor.created.clear()

    forced = module.run(manifest, force=True)

    assert (forced.built, forced.reused) == (1, 0)
    # A current asset is reused in place rather than recreated; only its values are re-applied.
    assert editor.created == []
    assert editor.assets[ROOT + "/PM_metal"].sidecar is not None


def test_an_ancestor_changing_re_authors_only_the_descendant(tmp_path):
    """The recipe carries a chain digest, so a leaf re-authors when an ancestor's unit changes
    even though the leaf's own GLB hash did not."""

    editor = FakeEditor()
    module = _load(editor)
    entries = [_entry("metal"), _entry("canister", baseChain=["metal"])]
    manifest = _stage(tmp_path, entries)
    module.run(manifest)
    editor.created.clear()

    entries[1]["recipe"] = dict(entries[1]["recipe"], chainSha256="chain-changed")
    changed = module.run(_stage(tmp_path, entries))

    assert (changed.built, changed.reused) == (1, 1)
    assert changed.failures == []


# --- failure isolation ----------------------------------------------------------------------------


def test_a_rejected_sidecar_fails_one_entry_and_the_run_goes_on(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    editor.apply_refuses["PM_broken"] = "surface-property sidecar is not a JSON object"
    manifest = _stage(tmp_path, [_entry("metal"), _entry("broken"), _entry("wood")])

    report = module.run(manifest)

    assert (report.built, report.reused) == (2, 0)
    assert [row["unit"] for row in report.failures] == ["vtmb:surface-property:broken"]
    assert "not a JSON object" in report.failures[0]["reason"]
    # The failed asset is never stamped, so the next run retries it.
    assert "ElysiumRecipe" not in editor.assets[ROOT + "/PM_broken"].metadata
    assert _report(tmp_path)["failed"][0]["assetPath"] == ROOT + "/PM_broken"


def test_a_refused_creation_fails_that_entry(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    editor.creates_class["PM_nope"] = None
    report = module.run(_stage(tmp_path, [_entry("nope"), _entry("metal")]))

    assert report.built == 1
    assert "create_asset produced no asset" in report.failures[0]["reason"]


def test_an_asset_of_the_wrong_class_is_deleted_and_recreated(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    editor.assets[ROOT + "/PM_metal"] = FakeAsset(ROOT + "/PM_metal", "PhysicalMaterial")

    report = module.run(_stage(tmp_path, [_entry("metal")]))

    assert report.failures == []
    assert ROOT + "/PM_metal" in editor.deleted
    assert editor.created == [("PM_metal", ROOT, "ElysiumPhysicalMaterial")]
    assert editor.assets[ROOT + "/PM_metal"].class_name == "ElysiumPhysicalMaterial"


def test_a_failed_save_fails_the_entry(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    editor.save_refuses.add(ROOT + "/PM_metal")

    report = module.run(_stage(tmp_path, [_entry("metal")]))

    assert report.built == 0
    assert "save failed" in report.failures[0]["reason"]


# --- pruning ---------------------------------------------------------------------------------------


def test_pruning_removes_what_the_manifest_neither_names_nor_protects(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    editor.assets[ROOT + "/PM_retired"] = FakeAsset(ROOT + "/PM_retired", "ElysiumPhysicalMaterial")
    editor.assets[ROOT + "/PM_retired"].metadata["ElysiumProducer"] = 'surface-properties'
    editor.assets[ROOT + "/PM_unreadable"] = FakeAsset(ROOT + "/PM_unreadable",
                                                       "ElysiumPhysicalMaterial")
    editor.assets[ROOT + "/PM_unreadable"].metadata["ElysiumProducer"] = 'surface-properties'
    manifest = _stage(tmp_path, [_entry("metal")], keep=[ROOT + "/PM_unreadable"])

    report = module.run(manifest)

    assert report.pruned == 1
    assert editor.deleted == [ROOT + "/PM_retired"]
    # A unit the stage could not resolve keeps the asset it already had.
    assert ROOT + "/PM_unreadable" in editor.assets


# --- the manifest contract ---------------------------------------------------------------------------


def test_a_manifest_the_script_cannot_execute_is_refused_whole(tmp_path):
    module = _load(FakeEditor())

    def refused(**over):
        body = {"schemaVersion": "1.0.0", "packageRoot": ROOT, "pruneScope": ROOT + "/",
                "keep": [], "assets": [_entry("metal")]}
        body.update(over)
        path = tmp_path / "bad.json"
        path.write_text(json.dumps(body), encoding="utf-8")
        return str(path)

    for over in (
        {"schemaVersion": "9.9.9"},
        {"packageRoot": "ElysiumBaked/SurfaceProperties"},
        {"assets": {}},
        {"pruneScope": "/Game/Elsewhere/"},
        {"keep": ["/Game/Elsewhere/PM_x"]},
        {"assets": [_entry("metal", assetPath="/Game/Elsewhere/PM_metal")]},
        {"assets": [_entry("metal", **{"class": "PhysicalMaterial"})]},
        {"assets": [_entry("metal"), _entry("metal")]},
    ):
        with pytest.raises(module.ManifestError):
            module.load_manifest(refused(**over))

    incomplete = dict(_entry("metal"))
    incomplete.pop("recipe")
    with pytest.raises(module.ManifestError):
        module.load_manifest(refused(assets=[incomplete]))


def test_the_command_line_reader_takes_a_quoted_path_and_a_zero_flag(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    editor.command_line = (
        'UnrealEditor-Cmd.exe -run=pythonscript '
        '"-ImportSurfaceProperties=C:/work root/import/manifest.json" -ImportForce=0')

    assert module.cmdline_arg("ImportSurfaceProperties") == "C:/work root/import/manifest.json"
    assert module.flag(module.cmdline_arg("ImportForce")) is False
    assert module.flag("1") and module.flag("true") and module.flag("YES")
    assert not module.flag("") and not module.flag("no")
