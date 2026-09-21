"""The editor half of the rope lane (0018 story 21-3), against a fake `unreal`.

`bake_ropes.author` spawns one `AElysiumRopeActor` per staged segment and hands it the eight facts
through the actor's own `UFUNCTION` setter; the bake verifier's cable check is pure. Both run here
with no live editor. The staging half is `test_map_ropes.py`.
"""
from __future__ import annotations

import importlib.util
import os
import sys
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]


class FakeActor:
    def __init__(self, cls, location):
        self.cls, self.location = cls, location
        self.tags, self.calls = [], {}

    def __getattr__(self, name):
        if name in ("configure_rope", "set_actor_label", "set_folder_path"):
            def call(*args):
                self.calls[name] = args
                return True
            return call
        raise AttributeError(name)


class FakeActors:
    def __init__(self):
        self.spawned = []

    def spawn_actor_from_class(self, cls, location, rotation=None):
        actor = FakeActor(cls, location)
        self.spawned.append(actor)
        return actor


def _load(name):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_" + name, REPO / "pipeline" / "unreal" / (name + ".py"))
    return spec, importlib.util.module_from_spec(spec)


@pytest.fixture()
def bake():
    fake = SimpleNamespace(
        Vector=lambda x=0.0, y=0.0, z=0.0: (x, y, z),
        ElysiumRopeActor=type("ElysiumRopeActor", (), {}),
        log=lambda *a, **k: None,
    )
    spec, module = _load("bake_ropes")
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        spec.loader.exec_module(module)
    return module, fake


PAYLOAD = {
    "version": 1, "map": "synthetic", "sha256": "abc",
    "counts": {"segments": 2, "nodes": 3, "materials": 2},
    "rows": [
        {"index": 0, "materialId": "vtmb:material:cable/cable",
         "aCm": [1.0, 2.0, 3.0], "bCm": [4.0, 5.0, 6.0],
         "widthCm": 2.54, "restCm": 203.2, "nodes": 10, "texScale": 0.2, "flags": 0},
        {"index": 1, "materialId": "vtmb:material:cable/chainb",
         "aCm": [7.0, 8.0, 9.0], "bCm": [7.0, 8.0, -9.0],
         "widthCm": 12.7, "restCm": 520.7, "nodes": 2, "texScale": 0.8, "flags": 1},
    ],
}


def test_author_places_one_actor_per_segment(bake):
    module, fake = bake
    actors = FakeActors()
    assert module.author(actors, PAYLOAD) == 2
    cable, chain = actors.spawned
    assert cable.cls is fake.ElysiumRopeActor
    # The actor stands at A, which is what `BuildRopes` reads back as the cable's own location.
    assert cable.location == (1.0, 2.0, 3.0)
    assert cable.calls["configure_rope"] == (
        0, "vtmb:material:cable/cable", (1.0, 2.0, 3.0), (4.0, 5.0, 6.0),
        2.54, 203.2, 10, 0.2, 0)
    assert cable.tags == ["elysium.rope", "elysium.src=0"]
    assert cable.calls["set_folder_path"] == ("Ropes",)
    assert cable.calls["set_actor_label"] == ("Rope_0_cable",)
    # The material's own stem is what tells a washing line from a hanging chain in the Outliner.
    assert chain.calls["set_actor_label"] == ("Rope_1_chainb",)
    # The flag word reaches the setter: `Dangling` is what unpins the far end.
    assert chain.calls["configure_rope"][-1] == 1


def test_author_refuses_a_bad_payload(bake):
    module, _ = bake
    with pytest.raises(ValueError, match="unsupported"):
        module.author(FakeActors(), {"version": 2, "rows": []})
    twice = dict(PAYLOAD, rows=[PAYLOAD["rows"][0], PAYLOAD["rows"][0]])
    with pytest.raises(ValueError, match="staged twice"):
        module.author(FakeActors(), twice)
    stale = dict(PAYLOAD, rows=[dict(PAYLOAD["rows"][0], materialId="cable/cable")])
    with pytest.raises(ValueError, match="vtmb:material:"):
        module.author(FakeActors(), stale)


def _verify_module():
    """`bake_verify` against the editor surface it touches at import (`test_bake_verify_water.py`)."""
    spec, module = _load("bake_verify")
    editor = SimpleNamespace(
        does_directory_exist=lambda target: True, make_directory=lambda target: True,
        does_asset_exist=lambda target: False, load_asset=lambda target: None,
        list_assets=lambda package, recursive=True, include_folder=True: [])
    fake = SimpleNamespace(
        AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
        MaterialEditingLibrary=object(), GeometryScript_Collision=object(),
        EditorAssetLibrary=editor, Paths=SimpleNamespace(project_dir=lambda: str(REPO)),
        SystemLibrary=SimpleNamespace(get_command_line=lambda: ""),
        LinearColor=lambda *values: values,
        log=lambda *a, **k: None, log_error=lambda *a, **k: None, log_warning=lambda *a, **k: None)
    with mock.patch.dict(sys.modules, {"unreal": fake}), \
            mock.patch.dict(os.environ, {"ELYSIUM_WORK_ROOT": str(REPO)}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        with pytest.raises(SystemExit):
            spec.loader.exec_module(module)   # main() refuses: no -BakeMaps= on the command line
    return module


def _facts(row, **overrides):
    facts = {key: row[key] for key in
             ("materialId", "aCm", "bCm", "widthCm", "restCm", "nodes", "texScale", "flags")}
    facts.update(overrides)
    return facts


def _placed(verify, rows=None, **overrides):
    return [([verify.ROPE_TAG, "%s%d" % (verify.ROPE_SOURCE_PREFIX, row["index"])],
             _facts(row, **overrides))
            for row in (PAYLOAD["rows"] if rows is None else rows)]


def test_verify_accepts_a_level_that_matches_its_rows():
    verify = _verify_module()
    assert verify.rope_errors("m", _placed(verify), PAYLOAD) == []


def test_verify_names_a_missing_and_an_unstaged_actor():
    verify = _verify_module()
    missing = verify.rope_errors("m", _placed(verify, rows=PAYLOAD["rows"][:1]), PAYLOAD)
    assert any("have no actor" in error for error in missing)

    stray = _placed(verify)
    stray.append(([verify.ROPE_TAG, verify.ROPE_SOURCE_PREFIX + "99"],
                  _facts(PAYLOAD["rows"][0])))
    assert any("was not staged" in error for error in verify.rope_errors("m", stray, PAYLOAD))

    twice = _placed(verify) + _placed(verify, rows=PAYLOAD["rows"][:1])
    assert any("placed twice" in error for error in verify.rope_errors("m", twice, PAYLOAD))

    untagged = [([verify.ROPE_TAG], _facts(PAYLOAD["rows"][0]))]
    assert any("carries no elysium.src=" in error
               for error in verify.rope_errors("m", untagged, PAYLOAD))


def test_verify_catches_a_lost_flag_word():
    """The flags are the eighth fact the story's text omits, and losing them unpins every
    `Dangling` cable's far end (`UCableComponent::bAttachEnd`)."""
    verify = _verify_module()
    errors = verify.rope_errors("m", _placed(verify, flags=0), PAYLOAD)
    assert any("flags differs" in error for error in errors)


def test_verify_catches_a_moved_endpoint_and_a_rebound_material():
    verify = _verify_module()
    moved = verify.rope_errors("m", _placed(verify, bCm=[0.0, 0.0, 0.0]), PAYLOAD)
    assert any("bCm differs" in error for error in moved)
    rebound = verify.rope_errors(
        "m", _placed(verify, materialId="vtmb:material:cable/rope"), PAYLOAD)
    assert any("placed as vtmb:material:cable/rope" in error for error in rebound)


def test_verify_folds_every_material_id_to_its_imported_instance():
    """R6.5, still asked of the actors rather than of a sidecar: the fold is the R5.4 rule and an
    id whose `MI_` is not imported is an error naming the package."""
    verify = _verify_module()
    errors, resolved = verify.rope_material_errors("m", {"vtmb:material:cable/chainb"})
    assert resolved == 0   # the fake editor imports nothing
    assert len(errors) == 1
    assert "/ElysiumBaked/Materials/cable/MI_chainb" in errors[0]
    stale, _ = verify.rope_material_errors("m", {"../shared/tex/cable_cable.png"})
    assert any("is not a vtmb:material:" in error for error in stale)
