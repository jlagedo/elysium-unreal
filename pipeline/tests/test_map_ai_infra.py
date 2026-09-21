"""The AI infrastructure stage (0018 story 2): one staged row per baked actor.

The synthetic cases pin the partition, the retail hint rule and the byte-exact keys; the corpus
cases (skipped without a V2 export root) stage every exported map and pin the three bake goals.
"""
from __future__ import annotations

import collections

import pytest

from elysium_pipeline import paths
from elysium_pipeline.exporters import UE_map_sidecars as producer
from elysium_pipeline.formats.map_entities_glb import model
from elysium_pipeline.importers import map_ai_infra as infra


def _output(position, key, value):
    """One `outputs[]` row as the entities unit publishes it -- `FUN_100ccf90`'s six fields, the
    empty-`input` substitution and the authored-0 `times` rewrite included."""
    fields = value.split(",")

    def at(index):
        return fields[index] if index < len(fields) else ""

    authored = model.atoi(at(4))
    return {"key": key, "keyValue": position, "raw": value,
            "target": at(0), "input": at(1) or model.DEFAULT_INPUT, "parameter": at(2),
            "delay": {"raw": at(3), "value": model.atof(at(3))[0]},
            "times": {"raw": at(4), "value": authored or model.UNLIMITED_TIMES},
            "python": at(5), "fieldCount": len(fields)}


def _row(index, pairs):
    """One entity as the unit publishes it. A key the class's datamap types as an output gets an
    `outputs[]` row back-linked to it, which is the only thing that makes it one."""
    classname = next((value for key, value in pairs if key == "classname"), "")
    folded = classname.strip().lower()
    outputs = []
    for position, (key, value) in enumerate(pairs):
        low = key.lower()
        if low.endswith(model.DISABLED_KEY_SUFFIX) or (folded, low) in model.NOT_OUTPUT_KEYS:
            continue
        if (model.OUTPUT_KEY.match(key) is not None
                or low in model.OUTPUT_KEYS_BY_CLASS.get(folded, frozenset())):
            outputs.append(_output(position, key, value))
    return {"index": index, "classname": classname,
            "keyValues": [{"index": i, "key": key.lower(), "sourceKey": key, "value": value}
                          for i, (key, value) in enumerate(pairs)],
            "outputs": outputs}


def _stage(*blocks):
    rows = [_row(i, pairs) for i, pairs in enumerate(blocks)]
    return infra.stage_rows("synthetic", rows)


def test_partition_by_family():
    payload = _stage(
        [("classname", "worldspawn")],
        [("classname", "info_node_patrol_point"), ("hinttype", "10000"), ("Group", "A1"),
         ("origin", "0 0 0")],
        [("classname", "intersting_place"), ("origin", "1 2 3")],
        [("classname", "intersting_place_conversation"), ("origin", "1 2 3")],
        [("classname", "npc_maker"), ("NPCType", "npc_VHuman"), ("origin", "0 0 0")],
        [("classname", "npc_VVampire"), ("origin", "0 0 0")],
        [("classname", "info_node"), ("origin", "0 0 0")],
    )
    assert [(row["index"], row["family"]) for row in payload["rows"]] == [
        (1, "hint"), (2, "place"), (3, "conversation"), (4, "maker"), (5, "npc")]
    assert payload["counts"] == {"hint": 1, "place": 1, "conversation": 1, "maker": 1, "npc": 1,
                                 "patrol": 1, "actors": 5}


def test_retail_hint_rule():
    # CNodeEnt::Spawn 0x102d78d0 / FUN_102d7d30.
    assert infra.makes_hint("info_node_cover_low", [("hinttype", "101")])
    assert infra.makes_hint("info_hint", [("hinttype", "5")])
    assert not infra.makes_hint("info_hint", [("hinttype", "0"), ("Group", "x")])  # standalone
    assert infra.makes_hint("info_node", [("Group", "x")])      # plain node with a Group
    assert not infra.makes_hint("info_node", [("hinttype", "100")])  # forced to 0
    assert not infra.makes_hint("info_node_werewolf_hint", [("hinttype", "14999")])
    assert infra.makes_hint("info_node_werewolf_hint", [("hinttype", "15018")])
    assert infra.makes_hint("info_node_tzimisce", [("Group", "g")])  # spawns as info_node
    assert not infra.makes_hint("info_node_link", [("hinttype", "100")])  # CAI_DynamicLink


def test_keys_keep_order_repeats_and_drop_hoisted_and_outputs():
    payload = _stage([
        ("classname", "npc_maker"), ("targetname", "m"), ("NPCType", "npc_VHuman"),
        ("OnSpawnNPC", "relay,Trigger,,0,-1"), ("model", "a.mdl"), ("model", "b.mdl"),
        ("OnSpawnNPC", "relay2,Trigger,,0.5,1"), ("origin", "0 0 0"),
    ])
    row = payload["rows"][0]
    assert row["keys"] == [["NPCType", "npc_VHuman"], ["model", "a.mdl"], ["model", "b.mdl"],
                           ["origin", "0 0 0"]]
    assert [(out["target"], out["delay"], out["times"]) for out in row["outputs"]] == [
        ("relay", 0.0, -1), ("relay2", 0.5, 1)]
    assert row["targetname"] == "m" and row["classname"] == "npc_maker"


def test_keys_are_the_transport_keys_after_its_fold():
    pairs = [("classname", "intersting_place"), ("max_time", "23523235.0"), ("min_bounds", "0  0 72"),
             ("max_time", ".5"), ("testflags", "4"), ("origin", "1 2 3")]
    row = _stage(pairs)["rows"][0]
    keys = producer.entity_keys(pairs)
    keys.pop("classname")
    folded = {}
    for key, value in row["keys"]:
        folded[key] = value
    assert folded == keys
    assert list(folded) == list(keys)


def test_sha256_tracks_content():
    first = _stage([("classname", "npc_VRat"), ("origin", "0 0 0")])
    second = _stage([("classname", "npc_VRat"), ("origin", "0 0 1")])
    assert first["sha256"] != second["sha256"]
    assert first["sha256"] == _stage([("classname", "npc_VRat"), ("origin", "0 0 0")])["sha256"]


def test_refusals():
    with pytest.raises(infra.MapAiInfraError, match="parented hint"):
        _stage([("classname", "info_node_hint"), ("hinttype", "5"), ("parentname", "p")])
    with pytest.raises(infra.MapAiInfraError, match="class-forced"):
        _stage([("classname", "info_node_cover_low"), ("hinttype", "100")])
    with pytest.raises(infra.MapAiInfraError, match="census"):
        _stage([("classname", "info_node"), ("Group", "g")])   # a hint the census never counted


def _export_root():
    try:
        root = paths.export_v2_root()
    except RuntimeError:
        pytest.skip("V2 export root not configured")
    if not (root / "maps").is_dir():
        pytest.skip("V2 entity units unavailable")
    return root


BAKE_GOAL_PINS = {
    "sp_tutorial_1": {"hint": 49, "place": 29, "conversation": 0, "maker": 14, "npc": 20,
                      "patrol": 37, "actors": 112},
    "sm_hub_1": {"hint": 274, "place": 76, "conversation": 0, "maker": 48, "npc": 38,
                 "patrol": 34, "actors": 436},
    "sp_soc_3": {"hint": 40, "place": 13, "conversation": 2, "maker": 0, "npc": 15,
                 "patrol": 25, "actors": 70},
}


@pytest.mark.parametrize("map_name", sorted(BAKE_GOAL_PINS))
def test_bake_goal_pins(map_name):
    root = _export_root()
    if not (root / "maps" / f"{map_name}.entities.glb").is_file():
        pytest.skip(f"{map_name} entity unit unavailable")
    payload = infra.stage_map(map_name, root)
    assert payload["counts"] == BAKE_GOAL_PINS[map_name]


def test_tutorial_patrol_point_pt1():
    root = _export_root()
    if not (root / "maps" / "sp_tutorial_1.entities.glb").is_file():
        pytest.skip("tutorial entity unit unavailable")
    rows = infra.stage_map("sp_tutorial_1", root)["rows"]
    pt1 = [row for row in rows if row["targetname"] == "pt1"]
    assert len(pt1) == 1 and pt1[0]["family"] == "place"
    keys = dict((key.lower(), value) for key, value in pt1[0]["keys"])
    assert (keys["group_id"], keys["enabled"], keys["min_time"], keys["max_time"]) == ("2", "1", "30.0", "60.0")


#: The three maps the text reconstruction refused outright until 0018 story 21-7 read the lump
#: structurally: their escaped output values re-escaped to a different length. They stage now, and
#: `test_every_exported_map_stages` covers all 108 without an exception list.
ONCE_LUMP_UNREADABLE = ("la_ventruetower_2", "la_ventruetower_3", "sp_giovanni_2b")


def test_every_exported_map_stages():
    root = _export_root()
    totals = collections.Counter()
    for path in sorted((root / "maps").glob("*.entities.glb")):
        map_name = path.name.split(".")[0]
        payload = infra.stage_map(map_name, root)
        indices = [row["index"] for row in payload["rows"]]
        assert len(indices) == len(set(indices)), map_name
        assert sum(payload["counts"][family] for family in infra.FAMILIES) == len(indices)
        for family in infra.FAMILIES:
            totals[family] += payload["counts"][family]
    # Every family is exercised somewhere in the corpus, conversation places included.
    assert all(totals[family] > 0 for family in infra.FAMILIES), totals


def test_the_three_once_unreadable_maps_stage():
    # 0018 story 21-7: the text reconstruction refused these three outright, for every lane.
    root = _export_root()
    for map_name in ONCE_LUMP_UNREADABLE:
        if not (root / "maps" / f"{map_name}.entities.glb").is_file():
            pytest.skip(f"{map_name} entity unit unavailable")
        payload = infra.stage_map(map_name, root)
        assert payload["entityRows"] > 0 and payload["rows"], map_name
