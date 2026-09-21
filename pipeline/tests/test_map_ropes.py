"""The rope stage (0018 story 21-3): one staged row per baked cable actor.

The synthetic cases pin the chain walk and the RE'd `CRopeKeyframe` arithmetic; the corpus cases
(skipped without a V2 export root) pin the five 21-2 maps' segment counts, which are the numbers
`bake map --verify` counts actors against.

Two contracts are checked together on purpose. `rope_rows` is the producer's derivation and
`rope_line` is the `.ropes` sidecar's byte format; the sidecar is an offline intermediate now, so
the only thing that keeps the two honest is that both are written from the same rows.

The editor half -- `bake_ropes.author` and `bake_verify.rope_errors` -- is `test_bake_map_ropes.py`,
which needs a fake `unreal`.
"""
from __future__ import annotations

import pytest

from elysium_pipeline import paths
from elysium_pipeline.exporters import UE_map_sidecars as producer
from elysium_pipeline.importers import map_ropes as ropes


def _stage(*blocks):
    return ropes.stage_rows("synthetic", [list(pairs) for pairs in blocks])


def _node(name, next_key=None, origin="0 0 0", **keys):
    pairs = [("classname", "keyframe_rope"), ("targetname", name), ("origin", origin)]
    if next_key is not None:
        pairs.append(("NextKey", next_key))
    pairs.extend((key, value) for key, value in keys.items())
    return pairs


def test_a_two_node_chain_is_one_segment():
    payload = _stage(
        _node("a", "b", "0 0 0"),
        _node("b", origin="100 0 0"),
    )
    assert payload["counts"]["nodes"] == 2
    assert payload["counts"]["segments"] == 1
    row = payload["rows"][0]
    assert row["index"] == 0
    # Source X -> Unreal X, Source Y -> -Unreal Y, inches -> cm.
    assert row["aCm"] == [0.0, 0.0, 0.0]
    assert row["bCm"] == [pytest.approx(254.0), 0.0, 0.0]


def test_a_chain_end_and_a_typo_emit_nothing():
    payload = _stage(
        _node("a", "nowhere", "0 0 0"),   # NextKey names no entity
        _node("b", origin="100 0 0"),     # no NextKey at all
    )
    assert payload["counts"] == {"segments": 0, "nodes": 2, "materials": 0}
    assert payload["rows"] == []


def test_move_rope_and_keyframe_rope_are_the_same_class():
    move = [("classname", "move_rope"), ("targetname", "a"), ("origin", "0 0 0"),
            ("NextKey", "b")]
    payload = _stage(move, _node("b", origin="100 0 0"))
    assert payload["counts"]["segments"] == 1


def test_next_key_resolves_case_insensitively_and_first_wins():
    """`CGlobalEntityList::FindEntityByName` folds case and takes the first match, over the whole
    entity list -- 16 NextKeys in the corpus name a non-rope entity."""
    payload = _stage(
        _node("a", "HOOK", "0 0 0"),
        [("classname", "info_target"), ("targetname", "hook"), ("origin", "100 0 0")],
        [("classname", "info_target"), ("targetname", "hook"), ("origin", "999 0 0")],
    )
    assert payload["rows"][0]["bCm"] == [pytest.approx(254.0), 0.0, 0.0]


def test_type_sets_the_node_count_not_subdiv():
    """`CRopeKeyframe::KeyValue` maps `Type` 0 -> 10, 1 -> 4, else 2, clamped to [2, 10]; `Subdiv`
    is a render cvar's business and sets nothing."""
    def nodes_for(**keys):
        return _stage(_node("a", "b", "0 0 0", **keys),
                      _node("b", origin="100 0 0"))["rows"][0]["nodes"]

    assert nodes_for(Type="0") == 10
    assert nodes_for(Type="1") == 4
    assert nodes_for(Type="2") == 2
    assert nodes_for(Type="7") == 2
    assert nodes_for(Subdiv="8") == producer.ROPE_DEFAULT_NODES   # no Type key -> the ctor default


def test_rest_length_applies_slack_twice_and_subtracts_a_hundred():
    """The two-stage integer computation: `RopeThink` folds `Slack` into `m_RopeLength`, then
    `RecomputeSprings` adds it again, subtracts a flat 100 and divides with C truncation."""
    payload = _stage(_node("a", "b", "0 0 0", Type="1", Slack="0"),
                     _node("b", origin="100 0 0"))
    row = payload["rows"][0]
    # span 100 units, 4 nodes: spring = int((100 + 0 - 100) / 3) = 0 -> a dead-straight chord.
    assert row["nodes"] == 4
    assert row["restCm"] == pytest.approx(0.0)

    payload = _stage(_node("a", "b", "0 0 0", Type="1", Slack="130"),
                     _node("b", origin="100 0 0"))
    row = payload["rows"][0]
    # rope_length = 100 + 130; spring = int((230 + 130 - 100) / 3) = 86; rest = 86 * 3 units.
    assert row["restCm"] == pytest.approx(86 * 3 * producer.INCH_TO_CM)


def test_flags_are_the_four_key_value_bits():
    payload = _stage(
        _node("a", "b", "0 0 0", Dangling="1", Breakable="1"),
        _node("b", origin="100 0 0"),
    )
    assert payload["rows"][0]["flags"] == 1 | 8


def test_rope_shader_overrides_rope_material():
    payload = _stage(
        _node("a", "b", "0 0 0", RopeMaterial="cable/cautiontape", RopeShader="2"),
        _node("b", origin="100 0 0"),
    )
    assert payload["rows"][0]["materialId"] == "vtmb:material:cable/chain"


def test_the_row_and_the_sidecar_line_are_the_same_twelve_facts():
    payload = _stage(_node("a", "b", "0 0 0", Type="2", Dangling="1"),
                     _node("b", origin="0 0 -100"))
    row = payload["rows"][0]
    tokens = producer.rope_line(row).split()
    assert len(tokens) == 12
    assert tokens[0] == row["materialId"]
    assert [float(value) for value in tokens[1:4]] == pytest.approx(row["aCm"])
    assert [float(value) for value in tokens[4:7]] == pytest.approx(row["bCm"])
    assert float(tokens[7]) == pytest.approx(row["widthCm"], abs=1e-4)
    assert float(tokens[8]) == pytest.approx(row["restCm"], abs=1e-4)
    assert int(tokens[9]) == row["nodes"]
    assert float(tokens[10]) == pytest.approx(row["texScale"], abs=1e-4)
    assert int(tokens[11]) == row["flags"]


def test_a_malformed_row_fails_the_bake():
    row = {"index": 0, "materialId": "cable/cable", "aCm": [0.0, 0.0, 0.0],
           "bCm": [1.0, 0.0, 0.0], "widthCm": 1.0, "restCm": 0.0, "nodes": 2,
           "texScale": 1.0, "flags": 0}
    with pytest.raises(ropes.MapRopesError, match="vtmb:material:"):
        ropes._validate("synthetic", row)
    with pytest.raises(ropes.MapRopesError, match=r"\[2, 10\]"):
        ropes._validate("synthetic", {**row, "materialId": "vtmb:material:cable/cable",
                                      "nodes": 11})
    with pytest.raises(ropes.MapRopesError, match="width"):
        ropes._validate("synthetic", {**row, "materialId": "vtmb:material:cable/cable",
                                      "widthCm": 0.0})


# --------------------------------------------------------------------------- the corpus


def _export_root():
    try:
        root = paths.export_v2_root()
    except RuntimeError:
        pytest.skip("V2 export root not configured")
    if not (root / "maps").is_dir():
        pytest.skip("V2 entity units unavailable")
    return root


#: Segment counts for the five maps 0018 story 21-2 stood up -- the numbers `bake map --verify`
#: counts rope actors against.
SEGMENT_PINS = {
    "sp_tutorial_1": 70,
    "sm_hub_1": 76,
    "sm_pawnshop_1": 21,
    "sp_theatre": 12,
    "sp_soc_3": 4,
}


@pytest.mark.parametrize("map_name", sorted(SEGMENT_PINS))
def test_segment_pins(map_name):
    root = _export_root()
    if not (root / "maps" / f"{map_name}.entities.glb").is_file():
        pytest.skip(f"{map_name} entity unit unavailable")
    payload = ropes.stage_map(map_name, root)
    assert payload["counts"]["segments"] == SEGMENT_PINS[map_name]
    assert len(payload["rows"]) == SEGMENT_PINS[map_name]
    assert [row["index"] for row in payload["rows"]] == list(range(SEGMENT_PINS[map_name]))


def test_soc_3_strings_four_chains_of_alphatest_chain():
    """`sp_soc_3`'s four cables are `cable/chainb`, an `$alphatest` texture ~47% cut out: rendering
    one opaque turns a chain into a solid tube with a chain painted on it."""
    root = _export_root()
    if not (root / "maps" / "sp_soc_3.entities.glb").is_file():
        pytest.skip("sp_soc_3 entity unit unavailable")
    rows = ropes.stage_map("sp_soc_3", root)["rows"]
    assert {row["materialId"] for row in rows} == {"vtmb:material:cable/chainb"}
    # Two Type-2 vertical drops and two ten-node spans, exactly as authored.
    assert sorted(row["nodes"] for row in rows) == [2, 2, 10, 10]
