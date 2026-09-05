"""`pipeline/unreal/import_textures.py`, the editor half of `uv run elysium import textures`,
exercised against a faked editor.

The script is an editor entry point, so the module is loaded with a fake `unreal` whose asset
registry, import tasks, provenance library and enums record what the script asked of them. The
fake keeps one dict of assets keyed by package path; stamping through `set_metadata_tag` and
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
ROOT = "/ElysiumBaked/Textures"


# --- the fake editor ----------------------------------------------------------------------------


class FakeAsset:
    def __init__(self, path, class_name, extent, fmt="PF_DXT5"):
        self.path = path
        self.class_name = class_name
        self.extent = extent            # (width, height, slices, mips)
        self.fmt = fmt
        self.props = {}
        self.metadata = {}
        self.provenance = None
        self.events = []                # ("set", prop) and ("built_extent",), in call order

    def get_class(self):
        return SimpleNamespace(get_name=lambda: self.class_name)

    def get_path_name(self):
        return "%s.%s" % (self.path, self.path.rsplit("/", 1)[-1])

    def set_editor_property(self, name, value):
        self.props[name] = value
        self.events.append(("set", name))

    def set_editor_properties(self, values):
        # One notification for the batch in the editor; the fake records the same per-property
        # events so ordering assertions read the same either way.
        for name, value in values.items():
            self.props[name] = value
            self.events.append(("set", name))


class FakeEditor:
    """One registry of assets plus the calls the script makes against it."""

    def __init__(self):
        self.assets = {}                # package path -> FakeAsset
        self.tasks = []                 # every AssetImportTask handed to import_asset_tasks
        self.deleted = []
        self.deleted_dirs = []
        self.saved = []
        self.logs, self.warnings, self.errors = [], [], []
        self.built_for = {}             # destination_name -> (class, extent, fmt) the "import" yields
        self.import_raises = None
        self.command_line = ""
        # Folders outlive the assets in them, as on disk: a folder is known from the first listing
        # that saw an asset below it until delete_directory removes it.
        self.known_folders = set()

    # -- import --
    def import_asset_tasks(self, tasks):
        if self.import_raises:
            raise self.import_raises
        for task in tasks:
            self.tasks.append(task)
            class_name, extent, fmt = self.built_for[task.destination_name]
            path = "%s/%s" % (task.destination_path, task.destination_name)
            self.assets[path] = FakeAsset(path, class_name, extent, fmt)

    # -- EditorAssetLibrary --
    def does_directory_exist(self, target):
        return True

    def make_directory(self, target):
        return True

    def does_asset_exist(self, target):
        return target in self.assets

    def load_asset(self, target):
        return self.assets.get(target)

    def list_assets(self, package, recursive=True, include_folder=False):
        prefix = package.rstrip("/") + "/"
        found = []
        for path, asset in self.assets.items():
            if not path.startswith(prefix):
                continue
            rel = path[len(prefix):]
            parts = rel.split("/")[:-1]
            for depth in range(1, len(parts) + 1):
                self.known_folders.add(prefix + "/".join(parts[:depth]) + "/")
            if not recursive and "/" in rel:
                continue
            found.append(asset.get_path_name())
        if include_folder:
            found.extend(sorted(f for f in self.known_folders if f.startswith(prefix)))
        return found

    def get_metadata_tag(self, asset, tag):
        return asset.metadata.get(tag, "")

    def set_metadata_tag(self, asset, tag, value):
        asset.metadata[tag] = value

    def save_asset(self, target, only_if_is_dirty=True):
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
        self.deleted_dirs.append(target)
        self.known_folders.discard(target)
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


class _Enum(SimpleNamespace):
    pass


def _enum(prefix, *names):
    return _Enum(**{name: "%s.%s" % (prefix, name) for name in names})


def _fake_unreal(editor):
    class AssetImportTask:
        pass

    def apply_json(texture, text):
        try:
            texture.provenance = json.loads(text)
        except ValueError as exc:
            return None, "bad json: %s" % exc
        return texture.provenance, ""

    def stamp_registry_tags(texture):
        if texture.provenance is None:
            return False, "no record"
        texture.metadata["ElysiumRole"] = texture.provenance.get("role", "")
        return True, ""

    def built_extent(texture):
        texture.events.append(("built_extent",))
        return texture.extent

    def write_built(texture, path):
        if texture.fmt == "PF_BC7":
            return False, "built format PF_BC7 is not one the measure lane decodes"
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        Path(path).write_bytes(b"DDS built")
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
            get_asset_tools=lambda: SimpleNamespace(import_asset_tasks=editor.import_asset_tasks)),
        MaterialEditingLibrary=object(),
        GeometryScript_Collision=object(),
        EditorAssetLibrary=library,
        AssetRegistryHelpers=SimpleNamespace(get_asset_registry=lambda: registry),
        SystemLibrary=SimpleNamespace(
            get_command_line=lambda: editor.command_line, collect_garbage=lambda: None),
        AssetImportTask=AssetImportTask,
        # `PyCore.cpp`'s module-level sweep, the one `import_textures._collect_garbage` calls.
        collect_garbage=lambda: None,
        TextureCompressionSettings=_enum("TC", "TC_DEFAULT", "TC_EDITOR_ICON", "TC_VECTOR_DISPLACEMENTMAP"),
        TextureMipGenSettings=_enum("TMGS", "TMGS_LEAVE_EXISTING_MIPS", "TMGS_NO_MIPMAPS"),
        TextureFilter=_enum("TF", "TF_DEFAULT", "TF_NEAREST", "TF_TRILINEAR"),
        TextureLossyCompressionAmount=_enum("TLCA", "TLCA_NONE"),
        TextureAddress=_enum("TA", "TA_WRAP", "TA_CLAMP"),
        ElysiumTextureProvenance=SimpleNamespace(apply_json=apply_json, stamp_registry_tags=stamp_registry_tags),
        ElysiumTextureImportLibrary=SimpleNamespace(
            built_extent=built_extent,
            built_pixel_format=lambda texture: texture.fmt,
            write_built_mip_zero_as_dds=write_built),
        log=editor.logs.append,
        log_warning=editor.warnings.append,
        log_error=editor.errors.append,
    )


def _load(editor):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_import_textures", REPO / "pipeline/unreal/import_textures.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    fake = _fake_unreal(editor)
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        try:
            spec.loader.exec_module(module)   # main() exits at once: no -ImportTextures=
        except SystemExit:
            pass
    # bake_lib was bound to the fake at import; keep it that way for the module's lifetime.
    module.unreal = fake
    return module


# --- staging fixtures ---------------------------------------------------------------------------


def _entry(name, directory="hud/signs", klass="Texture2D", **over):
    stem = name[2:] if name.startswith("T_") else name[3:]
    entry = {
        "assetPath": "%s/%s/%s" % (ROOT, directory, name),
        "class": klass,
        "dds": "%s/%s.dds" % (directory, stem),
        "provenance": "%s/%s.provenance.json" % (directory, stem),
        "unit": "vtmb:texture:%s/%s" % (directory, stem),
        "unitGlb": "textures/%s/%s.glb" % (directory, stem),
        "unitSha256": "sha-" + stem,
        "twinOf": None,
        "role": "colour",
        "roleEvidence": ["$basetexture"],
        "roleConflict": False,
        "srgb": True,
        "compression": "default",
        "mipGen": "leave-existing",
        "addressX": "wrap",
        "addressY": "wrap",
        "filter": "default",
        "neverStream": False,
        "expected": {"width": 64, "height": 32, "mips": 7, "faces": 1, "slices": 1},
        "recipe": {"unitSha256": "sha-" + stem, "settingsVersion": "elysium-texture-import-v1",
                   "role": "colour", "srgb": True, "compression": "default", "twin": False},
    }
    entry.update(over)
    return entry


def _stage(tmp_path, editor, entries, built=None, select=None, prune_scope=None, keep=()):
    """Write a manifest plus empty DDS/sidecar files, and tell the fake what each import yields."""
    for entry in entries:
        dds = tmp_path / entry["dds"]
        dds.parent.mkdir(parents=True, exist_ok=True)
        dds.write_bytes(b"DDS ")
        (tmp_path / entry["provenance"]).write_text(
            json.dumps({"assetId": entry["unit"], "role": entry["role"]}), encoding="utf-8")
        name = entry["assetPath"].rsplit("/", 1)[-1]
        expected = entry["expected"]
        slices = 6 if entry["class"] == "TextureCube" else expected.get("slices", 1)
        default = (entry["class"], (expected["width"], expected["height"], slices, expected["mips"]), "PF_DXT5")
        editor.built_for[name] = (built or {}).get(name, default)
    if prune_scope is None:
        prune_scope = ROOT + "/" + (select.strip("/") + "/" if select else "")
    manifest = {"schemaVersion": "1.0.0", "settingsVersion": "elysium-texture-import-v1",
                "packageRoot": ROOT, "select": select, "pruneScope": prune_scope,
                "keep": list(keep), "stageFailures": [], "assets": entries}
    path = tmp_path / "manifest.json"
    path.write_text(json.dumps(manifest), encoding="utf-8")
    return str(path)


def _report(tmp_path):
    return json.loads((tmp_path / "import_report.json").read_text(encoding="utf-8"))


# --- settings mapping ---------------------------------------------------------------------------


def test_settings_follow_the_manifest_entry(tmp_path):
    module = _load(FakeEditor())
    colour = module.settings_for(_entry("T_a"))
    assert colour["srgb"] is True
    assert colour["compression_settings"] == "TC.TC_DEFAULT"
    assert colour["mip_gen_settings"] == "TMGS.TMGS_LEAVE_EXISTING_MIPS"
    assert colour["lossy_compression_amount"] == "TLCA.TLCA_NONE"
    assert colour["filter"] == "TF.TF_DEFAULT"
    assert colour["address_x"] == "TA.TA_WRAP" and colour["address_y"] == "TA.TA_WRAP"
    assert colour["never_stream"] is False

    mask = module.settings_for(_entry(
        "T_m", srgb=False, compression="uncompressed", mipGen="no-mipmaps", filter="nearest",
        addressX="clamp", addressY="clamp", neverStream=True))
    assert mask["srgb"] is False
    assert mask["compression_settings"] == "TC.TC_VECTOR_DISPLACEMENTMAP"
    assert mask["mip_gen_settings"] == "TMGS.TMGS_NO_MIPMAPS"
    assert mask["filter"] == "TF.TF_NEAREST"
    assert mask["address_x"] == "TA.TA_CLAMP" and mask["never_stream"] is True

    ui = module.settings_for(_entry("T_u", srgb=True, compression="uncompressed", filter="trilinear"))
    assert ui["compression_settings"] == "TC.TC_EDITOR_ICON"
    assert ui["filter"] == "TF.TF_TRILINEAR"

    cube = module.settings_for(_entry("TC_sky", klass="TextureCube"))
    assert "address_x" not in cube and "address_y" not in cube
    array = module.settings_for(_entry("TA_tv", klass="Texture2DArray"))
    assert "address_x" in array

    with pytest.raises(module.ManifestError):
        module.settings_for(_entry("T_x", compression="bc7"))
    with pytest.raises(module.ManifestError):
        module.settings_for(_entry("T_x", filter="anisotropic"))


# --- the run -------------------------------------------------------------------------------------


def test_a_fresh_run_imports_every_entry_and_finishes_each(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    entries = [
        _entry("T_notepad_yellow"),
        _entry("T_mask", srgb=False, role="data-mask", addressX="clamp"),
        _entry("TC_sky", directory="skybox", klass="TextureCube",
               expected={"width": 256, "height": 256, "mips": 9, "faces": 6, "slices": 6}),
    ]
    manifest = _stage(tmp_path, editor, entries)

    report = module.run(manifest, force=False, measure=True)

    assert report.failures == []
    assert (report.built, report.reused, report.measured) == (3, 0, 3)
    assert editor.scanned == [ROOT]
    assert [task.destination_name for task in editor.tasks] == ["T_notepad_yellow", "T_mask", "TC_sky"]
    assert editor.tasks[0].filename.endswith("notepad_yellow.dds")
    assert editor.tasks[0].destination_path == ROOT + "/hud/signs"
    assert editor.tasks[0].automated and editor.tasks[0].replace_existing and not editor.tasks[0].save

    notepad = editor.assets[entries[0]["assetPath"]]
    assert notepad.props["srgb"] is True and notepad.props["address_x"] == "TA.TA_WRAP"
    mask = editor.assets[entries[1]["assetPath"]]
    assert mask.props["srgb"] is False and mask.props["address_x"] == "TA.TA_CLAMP"
    sky = editor.assets[entries[2]["assetPath"]]
    assert "address_x" not in sky.props

    # Provenance applied, registry tags published, recipe stamped, asset saved.
    assert notepad.provenance["assetId"] == "vtmb:texture:hud/signs/notepad_yellow"
    assert notepad.metadata["ElysiumRole"] == "colour"
    assert notepad.metadata["ElysiumRecipe"] == module.bl.recipe_fingerprint(
        "textures", entries[0]["assetPath"], entries[0]["recipe"])
    assert sorted(editor.saved) == sorted(e["assetPath"] for e in entries)

    # The built mip 0 sits beside the staged DDS for the measure phase.
    assert (tmp_path / "hud/signs/notepad_yellow.built.dds").is_file()
    assert (tmp_path / "skybox/sky.built.dds").is_file()
    assert _report(tmp_path)["imported"] == 3


def test_a_second_run_reuses_every_stamped_asset(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path, editor, [_entry("T_a"), _entry("T_b")])
    module.run(manifest)
    editor.tasks.clear()

    second = module.run(manifest)

    assert second.failures == []
    assert (second.built, second.reused) == (0, 2)
    assert editor.tasks == []


def test_force_rebuilds_a_current_asset(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path, editor, [_entry("T_a")])
    module.run(manifest)
    editor.tasks.clear()

    forced = module.run(manifest, force=True)

    assert (forced.built, forced.reused) == (1, 0)
    assert len(editor.tasks) == 1


def test_a_changed_unit_re_imports_only_itself(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    entries = [_entry("T_a"), _entry("T_b")]
    manifest = _stage(tmp_path, editor, entries)
    module.run(manifest)
    editor.tasks.clear()

    entries[1]["recipe"]["unitSha256"] = "sha-b-changed"
    manifest = _stage(tmp_path, editor, entries)
    again = module.run(manifest)

    assert (again.built, again.reused) == (1, 1)
    assert [task.destination_name for task in editor.tasks] == ["T_b"]


def test_an_asset_of_the_wrong_class_is_deleted_and_rebuilt(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    entry = _entry("TC_sky", directory="skybox", klass="TextureCube",
                   expected={"width": 256, "height": 256, "mips": 9, "faces": 6, "slices": 6})
    manifest = _stage(tmp_path, editor, [entry])
    # A 2D asset already sits at the cube's path, stamped with the very fingerprint the run computes.
    stale = FakeAsset(entry["assetPath"], "Texture2D", (256, 256, 1, 9))
    stale.metadata["ElysiumRecipe"] = module.bl.recipe_fingerprint("textures", entry["assetPath"], entry["recipe"])
    editor.assets[entry["assetPath"]] = stale

    report = module.run(manifest)

    assert report.failures == []
    assert (report.built, report.reused) == (1, 0)
    assert entry["assetPath"] in editor.deleted
    assert editor.assets[entry["assetPath"]].class_name == "TextureCube"


def test_a_twin_imports_the_same_dds_under_its_own_name_and_is_not_measured(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    colour = _entry("T_galply", directory="wood", roleConflict=True, roleEvidence=["$basetexture", "$envmapmask"])
    twin = _entry("T_galply_linear", directory="wood", srgb=False, role="data-mask",
                  twinOf=colour["assetPath"], roleConflict=True,
                  dds="wood/galply.dds", provenance="wood/galply_linear.provenance.json",
                  recipe={"unitSha256": "sha-galply", "settingsVersion": "elysium-texture-import-v1",
                          "role": "data-mask", "srgb": False, "compression": "default", "twin": True})
    manifest = _stage(tmp_path, editor, [colour, twin])

    report = module.run(manifest, measure=True)

    assert report.failures == []
    assert (report.built, report.measured) == (2, 1)
    names = {task.destination_name: task.filename for task in editor.tasks}
    assert names["T_galply"] == names["T_galply_linear"]
    assert editor.assets[twin["assetPath"]].props["srgb"] is False
    assert editor.assets[colour["assetPath"]].props["srgb"] is True
    assert (tmp_path / "wood/galply.built.dds").is_file()
    assert not (tmp_path / "wood/galply_linear.built.dds").exists()


def test_a_built_extent_mismatch_fails_that_entry_and_the_run_goes_on(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    entries = [_entry("T_good"), _entry("T_bad"), _entry("T_also_good")]
    manifest = _stage(tmp_path, editor, entries,
                      built={"T_bad": ("Texture2D", (32, 32, 1, 6), "PF_DXT5")})

    report = module.run(manifest)

    assert report.built == 2
    assert [f["assetPath"] for f in report.failures] == [entries[1]["assetPath"]]
    assert "built 32x32, expected 64x32" in report.failures[0]["reason"]
    assert report.failures[0]["unit"] == "vtmb:texture:hud/signs/bad"
    assert editor.errors and "T_bad" in editor.errors[0]
    written = _report(tmp_path)
    assert written["imported"] == 2
    assert written["failed"] == [{"assetPath": entries[1]["assetPath"], "unit": "vtmb:texture:hud/signs/bad",
                                  "reason": report.failures[0]["reason"]}]


def test_fewer_built_mips_than_authored_is_a_failure(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    entry = _entry("T_a")
    manifest = _stage(tmp_path, editor, [entry], built={"T_a": ("Texture2D", (64, 32, 1, 1), "PF_DXT5")})

    report = module.run(manifest)

    assert report.built == 0
    assert "built 1 mips, unit authored 7" in report.failures[0]["reason"]


def test_a_short_chain_unreal_extended_is_recorded_not_failed(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    entry = _entry("T_short", expected={"width": 64, "height": 32, "mips": 3, "faces": 1, "slices": 1})
    manifest = _stage(tmp_path, editor, [entry], built={"T_short": ("Texture2D", (64, 32, 1, 7), "PF_DXT1")})

    report = module.run(manifest)

    assert report.failures == []
    assert report.built == 1
    assert report.short_chains == [
        {"assetPath": entry["assetPath"], "authoredMips": 3, "builtMips": 7, "builtFormat": "PF_DXT1"}]
    assert report.built_formats == {"PF_DXT1": 1}


def test_an_unmeasurable_built_format_is_recorded_not_failed(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    entry = _entry("T_odd")
    manifest = _stage(tmp_path, editor, [entry], built={"T_odd": ("Texture2D", (64, 32, 1, 7), "PF_BC7")})

    report = module.run(manifest, measure=True)

    assert report.failures == [] and report.built == 1 and report.measured == 0
    assert report.measure_skipped[0]["assetPath"] == entry["assetPath"]
    assert "PF_BC7" in report.measure_skipped[0]["reason"]


def test_an_import_call_that_raises_fails_its_chunk_only(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path, editor, [_entry("T_a"), _entry("T_b")])
    editor.import_raises = RuntimeError("editor fell over")

    report = module.run(manifest)

    assert report.built == 0 and len(report.failures) == 2
    assert all("import call raised" in f["reason"] for f in report.failures)
    assert (tmp_path / "import_report.json").is_file()


def test_a_rejected_sidecar_fails_the_entry(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    entry = _entry("T_a")
    manifest = _stage(tmp_path, editor, [entry])
    (tmp_path / entry["provenance"]).write_text("not json", encoding="utf-8")

    report = module.run(manifest)

    assert report.built == 0
    assert "provenance rejected" in report.failures[0]["reason"]
    # Nothing was stamped, so the next run tries again rather than reusing a half-finished asset.
    assert "ElysiumRecipe" not in editor.assets[entry["assetPath"]].metadata


def test_prune_deletes_unlisted_assets_below_the_root_and_nothing_else(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path, editor, [_entry("T_kept")])
    editor.assets[ROOT + "/old/dir/T_retired"] = FakeAsset(ROOT + "/old/dir/T_retired", "Texture2D", (4, 4, 1, 1))
    editor.assets[ROOT + "/old/dir/T_retired"].metadata["ElysiumProducer"] = 'textures'
    editor.assets["/ElysiumBaked/Shared/Textures/T_legacy"] = FakeAsset(
        "/ElysiumBaked/Shared/Textures/T_legacy", "Texture2D", (4, 4, 1, 1))
    editor.assets["/ElysiumBaked/Shared/Textures/T_legacy"].metadata["ElysiumProducer"] = 'textures'

    report = module.run(manifest)

    assert report.pruned == 1
    assert ROOT + "/old/dir/T_retired" not in editor.assets
    assert "/ElysiumBaked/Shared/Textures/T_legacy" in editor.assets
    assert ROOT + "/hud/signs/T_kept" in editor.assets
    assert editor.deleted_dirs == [ROOT + "/old/dir/", ROOT + "/old/"]


def test_prune_preserves_foreign_and_unstamped_assets_in_the_same_root(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path, editor, [_entry("T_kept")])
    for name, producer in (("T_owned", "textures"), ("TC_sky", "maps"), ("T_unknown", None)):
        path = ROOT + "/" + name
        editor.assets[path] = FakeAsset(path, "Texture2D", (4, 4, 1, 1))
        if producer:
            editor.assets[path].metadata["ElysiumProducer"] = producer
    report = module.run(manifest)
    assert report.pruned == 1
    assert ROOT + "/T_owned" not in editor.assets
    assert ROOT + "/TC_sky" in editor.assets and ROOT + "/T_unknown" in editor.assets
    assert report.as_dict()["foreign"] == report.as_dict()["unstamped"] == 1


@pytest.mark.parametrize("force", [False, True])
def test_foreign_asset_cannot_be_overwritten_even_when_forced_or_wrong_class(tmp_path, force):
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path, editor, [_entry("T_kept")])
    path = ROOT + "/hud/signs/T_kept"
    asset = FakeAsset(path, "Material", (4, 4, 1, 1))
    asset.metadata["ElysiumProducer"] = "characters"
    editor.assets[path] = asset
    report = module.run(manifest, force=force)
    assert report.built == 0 and len(report.failures) == 1
    assert "characters" in report.failures[0]["reason"]
    assert editor.assets[path] is asset and not editor.deleted


def test_a_selected_run_prunes_only_inside_the_manifests_scope(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path, editor, [_entry("T_kept")], select="hud/signs")
    for path in (ROOT + "/hud/signs/T_stale_here", ROOT + "/hud/signs/Deep/T_stale_deep",
                 ROOT + "/hud/signs2/T_neighbour", ROOT + "/hud/T_other_hud", ROOT + "/wood/T_galply"):
        editor.assets[path] = FakeAsset(path, "Texture2D", (4, 4, 1, 1))
        editor.assets[path].metadata["ElysiumProducer"] = 'textures'

    report = module.run(manifest)

    assert report.pruned == 2
    assert ROOT + "/hud/signs/T_stale_here" not in editor.assets
    assert ROOT + "/hud/signs/Deep/T_stale_deep" not in editor.assets
    # `hud/signs/` is a folder, not a prefix: `hud/signs2/` and `hud/` itself are out of scope.
    assert ROOT + "/hud/signs2/T_neighbour" in editor.assets
    assert ROOT + "/hud/T_other_hud" in editor.assets and ROOT + "/wood/T_galply" in editor.assets
    assert _report(tmp_path)["select"] == "hud/signs"

    # The scope is the manifest's, verbatim and case-insensitive -- the script recomputes nothing.
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path / "hud", editor, [_entry("T_kept", directory="hud")], select="hud",
                      prune_scope=ROOT + "/HUD/")
    for path in (ROOT + "/hud/T_stale", ROOT + "/hudson/T_river"):
        editor.assets[path] = FakeAsset(path, "Texture2D", (4, 4, 1, 1))
        editor.assets[path].metadata["ElysiumProducer"] = 'textures'
    report = module.run(manifest)
    assert report.pruned == 1
    assert ROOT + "/hudson/T_river" in editor.assets and ROOT + "/hud/T_stale" not in editor.assets


def test_the_report_has_the_documented_shape(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path, editor, [_entry("T_a")])
    module.run(manifest, measure=False)

    written = _report(tmp_path)
    # `imported`, `reused`, `pruned` and `failed` (a list) are the keys the CLI reads back.
    assert set(written) >= {"schemaVersion", "manifest", "packageRoot", "select", "imported", "reused",
                            "pruned", "measured", "failed", "shortChains", "builtFormats",
                            "measureSkipped", "seconds"}
    assert written["packageRoot"] == ROOT and written["select"] is None
    assert (written["imported"], written["measured"], written["failed"]) == (1, 0, [])
    assert written["builtFormats"] == {"PF_DXT5": 1}
    assert not (tmp_path / "hud/signs/a.built.dds").exists()


# --- the manifest and the entry point -----------------------------------------------------------


def test_a_manifest_the_script_cannot_execute_is_refused(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    path = tmp_path / "manifest.json"

    def refused(manifest):
        path.write_text(json.dumps(manifest), encoding="utf-8")
        with pytest.raises(module.ManifestError):
            module.load_manifest(str(path))

    good = {"schemaVersion": "1.0.0", "packageRoot": ROOT, "assets": [_entry("T_a")]}
    refused(dict(good, schemaVersion="0.9.0"))
    refused(dict(good, packageRoot="/ElysiumBaked/Textures/"))
    refused(dict(good, assets=[_entry("T_a", klass="VolumeTexture")]))
    refused(dict(good, assets=[dict(_entry("T_a"), assetPath="/Game/T_a")]))
    refused(dict(good, assets=[_entry("T_a"), _entry("T_a")]))
    refused(dict(good, select="/"))
    refused(dict(good, select=7))
    refused(dict(good, pruneScope="/ElysiumBaked/Shared/"))
    refused(dict(good, pruneScope=ROOT + "/hud"))            # a folder ends with a slash
    refused(dict(good, keep="/ElysiumBaked/Textures/T_x"))
    refused(dict(good, keep=["/Game/T_x"]))
    missing = _entry("T_a")
    del missing["expected"]
    refused(dict(good, assets=[missing]))
    path.write_text(json.dumps(good), encoding="utf-8")
    loaded = module.load_manifest(str(path))
    assert loaded["packageRoot"] == ROOT
    # A manifest without the optional keys defaults to the whole root and nothing protected.
    assert loaded["pruneScope"] == ROOT + "/" and loaded["keep"] == []


def test_main_reads_its_arguments_and_exits_non_zero_on_any_failure(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    entries = [_entry("T_good"), _entry("T_bad")]
    manifest = _stage(tmp_path, editor, entries, built={"T_bad": ("Texture2D", (1, 1, 1, 1), "PF_DXT5")})

    # The launcher quotes the path, as subprocess does for any argument carrying a space.
    editor.command_line = '-run=pythonscript "-ImportTextures=%s" -ImportMeasure=1' % manifest
    with pytest.raises(SystemExit) as failed:
        module.main()
    assert failed.value.code == 1
    assert len(_report(tmp_path)["failed"]) == 1
    assert (tmp_path / "hud/signs/good.built.dds").is_file()      # -ImportMeasure=1 was honoured

    # A clean manifest exits normally, and without -ImportMeasure nothing is written back.
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path / "clean", editor, [_entry("T_good")])
    editor.command_line = '"-ImportTextures=%s" -ImportForce=1' % manifest
    module.main()
    assert _report(tmp_path / "clean")["imported"] == 1
    assert not (tmp_path / "clean/hud/signs/good.built.dds").exists()

    editor.command_line = ""
    with pytest.raises(SystemExit):
        module.main()

    bad = tmp_path / "bad.json"
    bad.write_text("{}", encoding="utf-8")
    editor.command_line = '"-ImportTextures=%s"' % bad
    with pytest.raises(SystemExit) as refused:
        module.main()
    assert refused.value.code == 1

    # Unreadable or unparseable manifests are refusals too, not tracebacks.
    editor.command_line = '"-ImportTextures=%s"' % (tmp_path / "does-not-exist.json")
    with pytest.raises(SystemExit) as refused:
        module.main()
    assert refused.value.code == 1
    bad.write_text("{not json", encoding="utf-8")
    editor.command_line = '"-ImportTextures=%s"' % bad
    with pytest.raises(SystemExit) as refused:
        module.main()
    assert refused.value.code == 1


def test_switches_spelled_off_stay_off(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    assert [module.flag(v) for v in ("1", "true", "YES", "on", '"1"')] == [True] * 5
    assert [module.flag(v) for v in ("0", "false", "no", "off", "", "2")] == [False] * 6

    manifest = _stage(tmp_path, editor, [_entry("T_good")])
    editor.command_line = '"-ImportTextures=%s" -ImportForce=1 -ImportMeasure=0' % manifest
    module.main()
    assert _report(tmp_path)["imported"] == 1
    assert not (tmp_path / "hud/signs/good.built.dds").exists()      # -ImportMeasure=0 honoured
    editor.tasks.clear()
    editor.command_line = '"-ImportTextures=%s" -ImportForce=0' % manifest
    module.main()
    assert _report(tmp_path)["reused"] == 1 and editor.tasks == []   # -ImportForce=0 honoured


# --- review follow-ups: protected assets, settings-before-build, real enum names --------------


def test_prune_spares_the_assets_a_failed_stage_protects(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    lost = ROOT + "/hud/signs/T_lost"
    manifest = _stage(tmp_path, editor, [_entry("T_kept")],
                      keep=[lost, lost + "_linear", ROOT + "/hud/signs/TC_lost"])
    for path in (lost, lost + "_linear", ROOT + "/hud/signs/T_retired"):
        editor.assets[path] = FakeAsset(path, "Texture2D", (4, 4, 1, 1))
        editor.assets[path].metadata["ElysiumProducer"] = 'textures'

    report = module.run(manifest)

    assert report.failures == []
    assert report.pruned == 1
    assert ROOT + "/hud/signs/T_retired" not in editor.assets
    assert lost in editor.assets and lost + "_linear" in editor.assets
    assert ROOT + "/hud/signs/T_kept" in editor.assets


def test_settings_are_applied_before_the_build_is_read(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path, editor, [_entry("T_a", mipGen="no-mipmaps",
                                                expected={"width": 64, "height": 32, "mips": 1,
                                                          "faces": 1, "slices": 1})],
                      built={"T_a": ("Texture2D", (64, 32, 1, 1), "PF_DXT5")})

    report = module.run(manifest)

    assert report.failures == [] and report.short_chains == []
    events = editor.assets[ROOT + "/hud/signs/T_a"].events
    first_set = next(i for i, event in enumerate(events) if event[0] == "set")
    first_read = next(i for i, event in enumerate(events) if event == ("built_extent",))
    assert first_set < first_read
    assert ("set", "mip_gen_settings") in events[:first_read]

    # The wrong class is still refused before any setting is touched.
    editor = FakeEditor()
    module = _load(editor)
    manifest = _stage(tmp_path / "cls", editor, [_entry("TC_sky", directory="skybox", klass="TextureCube",
                                                        expected={"width": 8, "height": 8, "mips": 1,
                                                                  "faces": 6, "slices": 6})],
                      built={"TC_sky": ("Texture2D", (8, 8, 1, 1), "PF_DXT1")})
    report = module.run(manifest)
    assert "imported as Texture2D, expected TextureCube" in report.failures[0]["reason"]
    assert editor.assets[ROOT + "/skybox/TC_sky"].events == []


#: The Unreal Python names the script asks for, spelled out rather than read back from the fake,
#: so a typo in the fake's own enum table cannot vouch for itself.
EXPECTED_ENUM_NAMES = {
    "TextureCompressionSettings": {"TC_DEFAULT", "TC_EDITOR_ICON", "TC_VECTOR_DISPLACEMENTMAP"},
    "TextureMipGenSettings": {"TMGS_LEAVE_EXISTING_MIPS", "TMGS_NO_MIPMAPS"},
    "TextureLossyCompressionAmount": {"TLCA_NONE"},
    "TextureFilter": {"TF_DEFAULT", "TF_NEAREST", "TF_TRILINEAR"},
    "TextureAddress": {"TA_WRAP", "TA_CLAMP"},
}
ENUM_PREFIX_TO_TYPE = {"TC": "TextureCompressionSettings", "TMGS": "TextureMipGenSettings",
                       "TLCA": "TextureLossyCompressionAmount", "TF": "TextureFilter",
                       "TA": "TextureAddress"}


def _names_the_script_uses(module):
    """Every enum name `settings_for` asks the `unreal` module for, over the manifest vocabulary."""
    used = {name: set() for name in EXPECTED_ENUM_NAMES}
    variants = [
        _entry("T_a"),
        _entry("T_b", srgb=False, compression="uncompressed", mipGen="no-mipmaps", filter="nearest",
               addressX="clamp", addressY="clamp"),
        _entry("T_c", srgb=True, compression="uncompressed", filter="trilinear"),
        _entry("TA_d", klass="Texture2DArray"),
        _entry("TC_e", klass="TextureCube"),
    ]
    for entry in variants:
        for value in module.settings_for(entry).values():
            if isinstance(value, str) and "." in value:
                prefix, name = value.split(".", 1)
                used[ENUM_PREFIX_TO_TYPE[prefix]].add(name)
    return used


def test_the_script_asks_only_for_the_expected_enum_names():
    module = _load(FakeEditor())
    assert _names_the_script_uses(module) == EXPECTED_ENUM_NAMES


def _python_enum_name(cpp_name):
    """`TC_EditorIcon` -> `TC_EDITOR_ICON`, the way Unreal's Python layer spells a C++ enumerator."""
    import re
    prefix, _, rest = cpp_name.partition("_")
    return prefix + "_" + re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", rest).upper()


def test_the_expected_enum_names_exist_in_the_engine_headers():
    import os
    import re
    ue_root = os.environ.get("ELYSIUM_UE_ROOT", "").strip()
    header = Path(ue_root) / "Engine/Source/Runtime/Engine/Classes/Engine/TextureDefines.h" if ue_root else None
    if header is None or not header.is_file():
        pytest.skip("no Unreal engine root on this machine")
    text = header.read_text(encoding="utf-8", errors="replace")
    declared = set()
    for match in re.finditer(r"\b(TC|TMGS|TLCA|TF|TA)_[A-Za-z0-9]+\b", text):
        declared.add(_python_enum_name(match.group(0)))
    assert _python_enum_name("TC_VectorDisplacementmap") == "TC_VECTOR_DISPLACEMENTMAP"
    assert _python_enum_name("TMGS_LeaveExistingMips") == "TMGS_LEAVE_EXISTING_MIPS"
    for names in EXPECTED_ENUM_NAMES.values():
        missing = names - declared
        assert not missing, missing
