"""The editor half of the AI infrastructure lane (0018 story 2), against a fake `unreal`.

`bake_ai_infra.author` spawns one actor per staged row through the actor subsystem and hands each
its identity, its authored pairs and its outputs through the actors' own `UFUNCTION` setters; the
bake verifier's declared-set check is pure. Both run here with no live editor.
"""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]


class FakeStruct:
    def __init__(self, **values):
        self.__dict__["_data"] = dict(values)

    def set_editor_property(self, name, value):
        self._data[name] = value

    def get_editor_property(self, name):
        return self._data[name]


class FakeActor:
    def __init__(self, cls, location, rotation):
        self.cls, self.location, self.rotation = cls, location, rotation
        self.tags, self.calls = [], {}

    def __getattr__(self, name):
        if name in ("configure_baked_identity", "apply_baked_keyvalues", "set_baked_outputs",
                    "set_actor_label", "set_folder_path", "configure_declared_set",
                    "set_stage_sha256"):
            def call(*args):
                self.calls[name] = args
                return True
            return call
        raise AttributeError(name)


class FakeActors:
    def __init__(self):
        self.spawned = []

    def spawn_actor_from_class(self, cls, location, rotation=None):
        actor = FakeActor(cls, location, rotation)
        self.spawned.append(actor)
        return actor


def _fake_unreal():
    classes = {name: type(name, (), {}) for name in (
        "ElysiumHintActor", "ElysiumInterestingPlaceActor", "ElysiumConversationPlaceActor",
        "ElysiumNpcMakerActor", "ElysiumNpcPlacementActor", "ElysiumInfraIndex")}
    quat = lambda x, y, z, w: SimpleNamespace(rotator=lambda: ("rot", x, y, z, w))
    return SimpleNamespace(
        Vector=lambda x=0.0, y=0.0, z=0.0: (x, y, z),
        Quat=quat,
        ElysiumInfraOutput=FakeStruct,
        log=lambda *a, **k: None,
        **classes,
    )


def _load(name):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_" + name, REPO / "pipeline" / "unreal" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    return spec, module


@pytest.fixture()
def bake():
    fake = _fake_unreal()
    spec, module = _load("bake_ai_infra")
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        spec.loader.exec_module(module)
    return module, fake


PAYLOAD = {
    "version": 1, "map": "synthetic", "sha256": "abc",
    "counts": {"hint": 1, "place": 0, "conversation": 0, "maker": 1, "npc": 0},
    "rows": [
        {"index": 7, "family": "hint", "classname": "info_node_patrol_point", "targetname": "",
         "originCm": [1.0, 2.0, 3.0], "rotationQuat": [0.0, 0.0, 0.0, 1.0],
         "keys": [["hinttype", "10000"], ["Group", "A1"], ["group", "a1"]], "outputs": []},
        {"index": 9, "family": "maker", "classname": "npc_maker", "targetname": "blueblood_maker",
         "originCm": [4.0, 5.0, 6.0], "rotationQuat": [0.0, 0.0, 0.0, 1.0],
         "keys": [["NPCType", "npc_VHuman"], ["model", "a.mdl"]],
         "outputs": [{"name": "OnSpawnNPC", "target": "r", "input": "Trigger", "param": "",
                      "delay": 0.0, "times": 0, "python": ""}]},
    ],
}


def test_author_places_one_actor_per_row_and_the_index(bake):
    module, fake = bake
    actors = FakeActors()
    assert module.author(actors, PAYLOAD) == 2
    hint, maker, index = actors.spawned
    assert hint.cls is fake.ElysiumHintActor and maker.cls is fake.ElysiumNpcMakerActor
    assert index.cls is fake.ElysiumInfraIndex
    assert hint.location == (1.0, 2.0, 3.0)
    assert hint.calls["configure_baked_identity"] == (7, "info_node_patrol_point", "")
    # The authored pairs reach the C++ setter verbatim: order, repeats and spelling.
    assert hint.calls["apply_baked_keyvalues"] == (
        ["hinttype", "Group", "group"], ["10000", "A1", "a1"])
    assert hint.tags == ["elysium.infra.hint", "elysium.ent=7"]
    assert hint.calls["set_folder_path"] == ("AI/PatrolPoints",)
    assert hint.calls["set_actor_label"] == ("Patrol_7_a1",)
    (outputs,) = maker.calls["set_baked_outputs"]
    assert outputs[0].get_editor_property("times") == 0   # normalised once, at the def rebuild
    assert maker.calls["set_actor_label"] == ("Maker_9_blueblood_maker",)
    assert index.calls["configure_declared_set"] == (
        [7, 9], ["elysium.infra.hint", "elysium.infra.maker"])
    assert index.tags == ["elysium.infra.index"]


def test_author_refuses_a_bad_payload(bake):
    module, _ = bake
    with pytest.raises(ValueError, match="unsupported"):
        module.author(FakeActors(), {"version": 2, "rows": []})
    twice = dict(PAYLOAD, rows=[PAYLOAD["rows"][0], PAYLOAD["rows"][0]])
    with pytest.raises(ValueError, match="staged twice"):
        module.author(FakeActors(), twice)


def _verify_module():
    spec, module = _load("bake_verify")
    fake = SimpleNamespace(log=lambda *a, **k: None, log_error=lambda *a, **k: None,
                           log_warning=lambda *a, **k: None)
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        try:
            spec.loader.exec_module(module)
        except Exception as error:   # the verifier's module-level editor imports
            pytest.skip("bake_verify needs more of the editor at import: %s" % error)
    return module


def test_verify_declared_set():
    verify = _verify_module()
    good = [("ElysiumHintActor", ["elysium.infra.hint", "elysium.ent=7"], 7, "info_node_patrol_point"),
            ("ElysiumNpcMakerActor", ["elysium.infra.maker", "elysium.ent=9"], 9, "npc_maker")]
    assert verify.ai_infra_errors("m", good, PAYLOAD, 1) == []
    missing = verify.ai_infra_errors("m", good[:1], PAYLOAD, 1)
    assert any("have no actor" in error for error in missing)
    twice = verify.ai_infra_errors("m", good + good[:1], PAYLOAD, 1)
    assert any("placed twice" in error for error in twice)
    wrong = [("ElysiumNpcPlacementActor", ["elysium.infra.hint", "elysium.ent=7"], 7,
              "info_node_patrol_point"), good[1]]
    assert any("is a ElysiumNpcPlacementActor" in error
               for error in verify.ai_infra_errors("m", wrong, PAYLOAD, 1))
    assert any("index actors" in error for error in verify.ai_infra_errors("m", good, PAYLOAD, 0))
