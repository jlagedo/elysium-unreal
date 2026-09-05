"""The wield bake's per-stem recipe reuse, exercised against a faked editor.

`bake_wield.py` is an editor entry point, so the module is loaded with a fake `unreal` and a
temporary export root; `read_manifest` exits `main()` at once, with every definition made.
"""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
from unittest import mock


REPO = Path(__file__).resolve().parents[2]

WIELD = "/ElysiumBaked/Items/Wield"


def _fake_unreal(logs, warnings, errors):
    editor = SimpleNamespace(
        does_directory_exist=lambda target: True,
        make_directory=lambda target: True,
        does_asset_exist=lambda target: False,
        load_asset=lambda target: None,
        list_assets=lambda package, recursive=True, include_folder=True: [],
        get_metadata_tag=lambda asset, tag: "",
        set_metadata_tag=lambda asset, tag, value: None,
        save_asset=lambda target, only_if_is_dirty=True: True,
    )
    return SimpleNamespace(
        AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
        MaterialEditingLibrary=object(),
        GeometryScript_Collision=object(),
        EditorAssetLibrary=editor,
        SystemLibrary=SimpleNamespace(get_command_line=lambda: ""),
        AssetRegistryHelpers=SimpleNamespace(get_asset_registry=lambda: None),
        log=logs.append,
        log_warning=warnings.append,
        log_error=errors.append,
    )


def _load_bake_wield(fake_unreal, export_root):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_bake_wield", REPO / "pipeline/unreal/bake_wield.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"unreal": fake_unreal}), \
            mock.patch.dict(os.environ, {"ELYSIUM_EXPORT_ROOT": export_root}):
        # A copy loaded against the fake editor, so the real one cannot leak in from an
        # earlier import; patch.dict restores whatever was there.
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        try:
            spec.loader.exec_module(module)
        except SystemExit:
            pass
    return module


def _module(out, errors=None):
    """The bake module under a recording `unreal` double; `errors` collects what it logged."""
    return _load_bake_wield(_fake_unreal([], [], errors if errors is not None else []), out)


@staticmethod
def _model(eskm_rel="items/wield/w_test.eskm"):
    return {
        "eskm": eskm_rel,
        "binding": "socket_hand",
        "bone_count": 2,
        "materials": [{"name": "blade", "albedo": "key_a", "flags": []}],
        "skin_families": [],
    }


def test_fingerprint_is_stable_and_moves_with_its_inputs() -> None:
    with tempfile.TemporaryDirectory() as out:
        module = _module(out)
        source = Path(out) / "items" / "wield" / "w_test.eskm"
        source.parent.mkdir(parents=True)
        source.write_bytes(b"container v1")
        model = _model()
        table = {"key_a": "tex/blade.png"}

        first = module.stem_fingerprint("w_test", model, table)
        assert module.stem_fingerprint("w_test", model, table) == first

        # The container's bytes are an input...
        source.write_bytes(b"container v2")
        container_moved = module.stem_fingerprint("w_test", model, table)
        assert container_moved != first

        # ...so is the manifest row...
        repainted = _model()
        repainted["materials"][0]["flags"] = ["additive"]
        assert module.stem_fingerprint("w_test", repainted, table) != container_moved

        # ...and so is a re-pointed texture key.
        assert module.stem_fingerprint("w_test", model, {"key_a": "tex/other.png"}) != container_moved


def test_a_missing_container_digests_as_a_changed_input() -> None:
    with tempfile.TemporaryDirectory() as out:
        module = _module(out)
        model = _model("items/wield/absent.eskm")
        table: dict = {}
        absent = module.stem_fingerprint("w_test", model, table)
        source = Path(out) / "items" / "wield" / "absent.eskm"
        source.parent.mkdir(parents=True)
        source.write_bytes(b"now present")
        assert module.stem_fingerprint("w_test", model, table) != absent


def _stamped_mount(module, stamps, textures=True):
    """Point the fake registry at a folder of `stamps` ({object path: stored recipe});
    `textures` is whether the shared texture mount carries every asset asked for."""
    module.unreal.EditorAssetLibrary.list_assets = (
        lambda package, recursive=True, include_folder=False: sorted(
            "%s.%s" % (path, path.rsplit("/", 1)[-1]) for path in stamps))
    module.unreal.EditorAssetLibrary.does_asset_exist = lambda target: textures
    module.bl.stored_recipe = lambda path, **kwargs: stamps.get(path, "")

WIELD_REUSE_TABLE = {"key_a": "tex/blade.png"}


def _current(module, force=False):
    return module.stem_is_current("w_test", _model(), WIELD_REUSE_TABLE, "fp", force)


def test_reuse_needs_every_asset_on_the_current_recipe() -> None:
    with tempfile.TemporaryDirectory() as out:
        module = _module(out)
        stamps = {
            "%s/w_test/SKEL_w_test" % WIELD: "fp",
            "%s/w_test/SK_w_test" % WIELD: "fp",
            "%s/w_test/MI_SK_w_test_blade" % WIELD: "fp",
        }
        _stamped_mount(module, stamps)
        assert _current(module)

        # One asset off the recipe makes the whole stem stale.
        stamps["%s/w_test/MI_SK_w_test_blade" % WIELD] = "older"
        assert not _current(module)


def test_a_stamped_skeleton_beside_no_mesh_is_stale() -> None:
    # The builders stamp as they save, so a build that failed after the skeleton
    # leaves one current-looking asset; the pair is the minimum a reuse accepts.
    with tempfile.TemporaryDirectory() as out:
        module = _module(out)
        _stamped_mount(module, {"%s/w_test/SKEL_w_test" % WIELD: "fp"})
        assert not _current(module)
        _stamped_mount(module, {"%s/w_test/SK_w_test" % WIELD: "fp"})
        assert not _current(module)


def test_a_texture_gone_from_the_mount_is_stale() -> None:
    with tempfile.TemporaryDirectory() as out:
        module = _module(out)
        stamps = {
            "%s/w_test/SKEL_w_test" % WIELD: "fp",
            "%s/w_test/SK_w_test" % WIELD: "fp",
        }
        _stamped_mount(module, stamps, textures=False)
        assert not _current(module)
        # A key the manifest's table does not carry is stale too: the build reports it.
        _stamped_mount(module, stamps)
        assert not module.stem_is_current("w_test", _model(), {}, "fp", False)


def test_an_empty_folder_is_stale() -> None:
    with tempfile.TemporaryDirectory() as out:
        module = _module(out)
        _stamped_mount(module, {})
        assert not _current(module)


def test_force_defeats_the_reuse_whole() -> None:
    with tempfile.TemporaryDirectory() as out:
        module = _module(out)
        stamps = {
            "%s/w_test/SKEL_w_test" % WIELD: "fp",
            "%s/w_test/SK_w_test" % WIELD: "fp",
        }
        _stamped_mount(module, stamps)
        assert _current(module)
        assert not _current(module, force=True)


def test_stamping_covers_only_the_assets_the_builders_left_unstamped() -> None:
    with tempfile.TemporaryDirectory() as out:
        module = _module(out)
        skeleton = "%s/w_test/SKEL_w_test" % WIELD
        instance = "%s/w_test/MI_SK_w_test_blade" % WIELD
        metadata = {skeleton: "fp"}  # the builder stamped it pre-save
        loaded = {skeleton: object(), instance: object()}
        saved: list[str] = []
        stamped: list[str] = []
        editor = module.unreal.EditorAssetLibrary
        editor.list_assets = (
            lambda package, recursive=True, include_folder=False: sorted(
                "%s.%s" % (path, path.rsplit("/", 1)[-1]) for path in loaded))
        editor.load_asset = lambda path: loaded.get(path)
        editor.get_metadata_tag = (
            lambda asset, tag: metadata.get(
                next(path for path, obj in loaded.items() if obj is asset), ""))
        module.bl.stamp_recipe = (
            lambda asset, fingerprint, **kwargs: stamped.append(
                next(path for path, obj in loaded.items() if obj is asset)))
        module.bl.save = lambda path: saved.append(path) or True

        failed: list[str] = []
        module.stamp_stem_assets("w_test", "fp", failed)
        assert failed == []
        assert stamped == [instance]
        assert saved == [instance]


def test_a_failed_stamp_save_is_loud_and_fails_the_stem() -> None:
    errors: list[str] = []
    with tempfile.TemporaryDirectory() as out:
        module = _module(out, errors)
        instance = "%s/w_test/MI_SK_w_test_blade" % WIELD
        editor = module.unreal.EditorAssetLibrary
        editor.list_assets = (
            lambda package, recursive=True, include_folder=False: [
                "%s.%s" % (instance, instance.rsplit("/", 1)[-1])])
        editor.load_asset = lambda path: object()
        editor.get_metadata_tag = lambda asset, tag: ""
        module.bl.stamp_recipe = lambda asset, fingerprint, **kwargs: None
        module.bl.save = lambda path: False

        failed: list[str] = []
        module.stamp_stem_assets("w_test", "fp", failed)
        assert failed == ["w_test"]
        assert any("could not be saved" in line for line in errors)
