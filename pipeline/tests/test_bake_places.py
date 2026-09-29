"""The editor half of the place set lane (0018 story 4), against a fake `unreal`.

`bake_places.author` creates `DA_<map>_Places` through the data-asset factory, hands the staged
block to the asset's own `AuthorJson` verbatim, stamps the recipe and saves; the bake verifier's
row-count check is pure. Both run here with no live editor.
"""
from __future__ import annotations

import importlib.util
import json
import os
import sys
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]

BLOCK = {
    "version": 1, "map": "synthetic", "numNodes": 2, "usedHullBits": 1,
    "places": [
        {"index": 0, "type": 2, "flags": 0, "origin": [2.54, -5.08, 7.62], "yaw": -90.0,
         "zOffsets": [0.254] * 22, "wcId": 3, "hint": 7},
        {"index": 1, "type": 2, "flags": 0, "origin": [0.0, 0.0, 0.0], "yaw": 0.0,
         "zOffsets": [0.254] * 22, "wcId": 4, "hint": -1},
    ],
    "pairing": {"nodeRows": 2, "outOfRange": [], "standalone": []},
    "crosswalkPairs": [],
    "wanderCaps": [{"hull": 0, "capUnits": 2000.0, "fromHuman": False}],
}


class FakePlaces:
    def __init__(self, accept=True):
        self.accept, self.json = accept, None

    def author_json(self, text):
        self.json = text
        return self.accept


class FakeEditor:
    def __init__(self, existing=None):
        self.existing, self.tags, self.saved, self.dirs = existing, {}, [], []

    def does_directory_exist(self, target):
        return target in self.dirs

    def make_directory(self, target):
        self.dirs.append(target)
        return True

    def does_asset_exist(self, target):
        return False

    def load_asset(self, target):
        return None

    def list_assets(self, package, recursive=True, include_folder=True):
        return []

    def set_metadata_tag(self, asset, tag, value):
        self.tags[tag] = value

    def save_loaded_asset(self, asset, only_if_is_dirty=True):
        self.saved.append(asset)
        return True


class FakeTools:
    def __init__(self, made):
        self.made, self.calls = made, []

    def create_asset(self, name, directory, cls, factory):
        self.calls.append((name, directory, cls, factory.props))
        return self.made


class FakeFactory:
    def __init__(self):
        self.props = {}

    def set_editor_property(self, name, value):
        self.props[name] = value


def _fake_unreal(existing=None, made=None):
    editor = FakeEditor()
    tools = FakeTools(made)
    return SimpleNamespace(
        AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: tools),
        MaterialEditingLibrary=object(),
        GeometryScript_Collision=object(),
        EditorAssetLibrary=editor,
        Paths=SimpleNamespace(project_dir=lambda: str(REPO)),
        SystemLibrary=SimpleNamespace(get_command_line=lambda: ""),
        DataAssetFactory=FakeFactory,
        ElysiumMapPlaces=type("ElysiumMapPlaces", (), {}),
        load_asset=lambda target: existing,
        log=lambda *a, **k: None,
        log_warning=lambda *a, **k: None,
        log_error=lambda *a, **k: None,
    ), editor, tools


def _load(name, fake, *, expect_exit=False):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_" + name, REPO / "pipeline" / "unreal" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"unreal": fake}), \
            mock.patch.dict(os.environ, {"ELYSIUM_WORK_ROOT": str(REPO)}):
        # `from pipeline.unreal import bake_lib` takes the package attribute before sys.modules,
        # so both go: `bake_lib` binds whichever `unreal` it was first imported under.
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        package = sys.modules.get("pipeline.unreal")
        if package is not None and hasattr(package, "bake_lib"):
            delattr(package, "bake_lib")
        if expect_exit:
            with pytest.raises(SystemExit):
                spec.loader.exec_module(module)
        else:
            spec.loader.exec_module(module)
    return module


def test_author_creates_the_asset_and_hands_it_the_staged_block_verbatim():
    made = FakePlaces()
    fake, editor, tools = _fake_unreal(existing=None, made=made)
    module = _load("bake_places", fake)
    path = module.author(BLOCK)
    assert path == "/ElysiumBaked/synthetic/DA_synthetic_Places"
    ((name, directory, cls, props),) = tools.calls
    assert (name, directory) == ("DA_synthetic_Places", "/ElysiumBaked/synthetic")
    assert cls is fake.ElysiumMapPlaces and props["data_asset_class"] is fake.ElysiumMapPlaces
    # The C++ reader gets the block itself: same keys, same values, nothing added or renamed.
    assert json.loads(made.json) == BLOCK
    assert editor.tags["ElysiumProducer"] == "map-places" and editor.tags["ElysiumRecipe"]
    assert editor.saved == [made]


def test_author_reuses_an_existing_asset_and_refuses_what_authorjson_refuses():
    existing = FakePlaces(accept=False)
    fake, editor, tools = _fake_unreal(existing=existing)
    module = _load("bake_places", fake)
    with pytest.raises(ValueError, match="AuthorJson refused"):
        module.author(BLOCK)
    assert tools.calls == [] and editor.saved == []


def test_the_digest_moves_with_any_value_and_not_with_key_order():
    fake, _, _ = _fake_unreal()
    module = _load("bake_places", fake)
    first = module.digest(BLOCK)
    reordered = dict(reversed(list(BLOCK.items())))
    assert module.digest(reordered) == first
    moved = json.loads(json.dumps(BLOCK))
    moved["places"][0]["hint"] = 8
    assert module.digest(moved) != first
    assert module.digest(None) is None


@pytest.fixture(scope="module")
def verify():
    fake, _, _ = _fake_unreal()
    return _load("bake_verify", fake, expect_exit=True)


def test_verify_accepts_one_row_per_staged_node(verify):
    assert verify.places_errors("synthetic", "/p", [object(), object()], 2, BLOCK) == []


def test_verify_names_a_missing_asset_a_short_asset_and_a_different_network(verify):
    assert "does not load" in verify.places_errors("synthetic", "/p", None, None, BLOCK)[0]
    assert "no staged place set" in verify.places_errors("synthetic", "/p", [], 0, None)[0]
    short = verify.places_errors("synthetic", "/p", [object()], 2, BLOCK)
    assert short == ["synthetic: /p has 1 rows for 2 nodes"]
    other = verify.places_errors("synthetic", "/p", [object()] * 3, 3, BLOCK)
    assert other == ["synthetic: /p holds 3 nodes, the staged graph 2"]
