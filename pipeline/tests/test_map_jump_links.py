"""Retail AIN filtering, the native endpoint representation (requirement 19), and the per-hull
record 0018/7 bakes: every jump link a disabled record, since no NPC can hold the jump capability."""
from copy import deepcopy

import pytest

from elysium_pipeline import paths
from elysium_pipeline.importers import map_jump_links as jump
from elysium_pipeline.validation import nav_gate


def _graph():
    return dict(header=dict(version=30, numHulls=22, usedHullBits=dict(value=1),
                            numNodes=2, totalNumLinks=1),
                nodes=[dict(index=i, origin=dict(source=[i * 100, 20, 30]),
                            hullOffsets=[-4.0] * 22, type=2) for i in range(2)],
                links=[dict(index=0, src=0, dst=1, fields=[0, 2] + [0] * 21)])


def test_human_jump_endpoints_include_hull_floor_offset_and_single_y_reflection():
    row = jump.project_graph(_graph(), "fixture")["links"][0]
    assert row["startCm"] == pytest.approx([0, -50.8, 66.04])
    assert row["endCm"] == pytest.approx([254, -50.8, 66.04])
    assert row["bidirectional"] and row["hull"] == 0


@pytest.mark.parametrize("field,value", [(0, 0x1000), (1, 0), (1, 1), (1, 4), (1, 8)])
def test_disabled_walk_fly_climb_and_disconnected_links_are_not_jump_links(field, value):
    graph = _graph()
    graph["links"][0]["fields"][field] = value
    assert jump.project_graph(graph, "fixture")["links"] == []


def test_other_hull_jump_does_not_make_a_human_jump_and_stale_info_is_not_disabled():
    graph = _graph()
    graph["links"][0]["fields"][1] = 0
    graph["links"][0]["fields"][20] = 2
    assert not jump.project_graph(graph, "fixture")["links"]
    graph = _graph()
    graph["header"]["usedHullBits"]["value"] = 1 << 19
    assert not jump.project_graph(graph, "fixture")["links"]
    graph = _graph()
    graph["links"][0]["fields"][0] = 1 | 2 | 0x2000
    assert len(jump.project_graph(graph, "fixture")["links"]) == 1


def test_reversed_duplicate_does_not_emit_a_second_shared_link():
    graph = _graph()
    reverse = deepcopy(graph["links"][0])
    reverse.update(index=1, src=1, dst=0)
    graph["links"].append(reverse)
    graph["header"]["totalNumLinks"] = 2
    payload = jump.project_graph(graph, "fixture")
    assert len(payload["links"]) == 1 and payload["excluded"]["duplicate"] == 1


@pytest.mark.parametrize("damage", ["endpoint", "fields", "position", "hull", "count", "node_type"])
def test_corrupt_graph_does_not_bake_a_plausible_but_wrong_edge(damage):
    graph = _graph()
    if damage == "endpoint": graph["links"][0]["dst"] = 90
    if damage == "fields": graph["links"][0]["fields"].pop()
    if damage == "position": graph["nodes"][0]["origin"]["source"][0] = float("nan")
    if damage == "hull": graph["nodes"][0]["hullOffsets"].pop()
    if damage == "count": graph["header"]["totalNumLinks"] = 10
    if damage == "node_type": graph["nodes"][0]["type"] = 4
    with pytest.raises(ValueError): jump.project_graph(graph, "fixture")


def test_endpoint_or_filter_change_invalidates_level_recipe():
    graph = _graph()
    first = jump.project_graph(graph, "fixture")["sha256"]
    graph["nodes"][0]["hullOffsets"][0] += 1
    assert jump.project_graph(graph, "fixture")["sha256"] != first
    graph["links"][0]["fields"][0] = jump.LINK_OFF
    assert jump.project_graph(graph, "fixture")["sha256"] != first


def _staged(map_name):
    try:
        root = paths.export_v2_root()
    except RuntimeError:
        pytest.skip("V2 export root not configured")
    if not (root / f"nav-graphs/{map_name}.glb").is_file():
        pytest.skip(f"{map_name} V2 navigation unit unavailable")
    return jump.stage_map(map_name, root)


def test_tutorial_decoded_ain_has_its_human_jump_connections():
    payload = _staged("sp_tutorial_1")
    # The PATCH's own graph, which is the one the patched install runs (0018 story 3): 203 nodes
    # and 429 links against the base game's packed 116 / 234. The first nine indices below are the
    # base graph's, unchanged -- the patch adds 87 nodes and its jumps sit past them.
    assert payload["sourceLinks"] == 429
    assert [row["index"] for row in payload["links"]] == [
        22, 24, 30, 88, 110, 115, 147, 163, 218, 237, 238, 256, 261, 278, 279, 281,
        283, 285, 287, 292, 390, 393, 399, 402, 407,
    ]
    assert payload["excluded"] == dict(nonJump=404, disabled=0, duplicate=0)


# --- 0018/7: per-hull verdicts; every jump link a record, none a route ----------------------------

def _two_hull_graph(rat_word):
    graph = _graph()
    graph["header"]["usedHullBits"]["value"] = 1 | (1 << 19)
    graph["links"][0]["fields"][1 + 19] = rat_word
    graph["nodes"][1]["hullOffsets"][19] = 2.0
    return graph


def test_every_used_hull_gets_a_verdict_in_used_hull_order():
    row = jump.project_graph(_two_hull_graph(1), "fixture")["links"][0]
    assert [entry["hull"] for entry in row["hulls"]] == [0, 19]
    human, rat = row["hulls"]
    assert human["motion"] == 2 and human["jumpOnly"] and human["capabilityUsable"] is False
    assert "0x102ff960 step 2" in human["refusedBy"]
    assert human["startCm"] == row["startCm"] and human["endCm"] == row["endCm"]
    # A ground word for the rat: not a jump there, so nothing for IsJumpLegal to be asked.
    assert rat == dict(hull=19, motion=1, jumpOnly=False)


def test_a_jump_only_hull_carries_its_own_hull_endpoints():
    row = jump.project_graph(_two_hull_graph(2), "fixture")["links"][0]
    rat = row["hulls"][1]
    assert rat["jumpOnly"] and rat["capabilityUsable"] is False
    # Node 1's rat offset is 2 units above the human's -4: the end rises 6 units = 15.24 cm.
    assert rat["endCm"][2] - row["endCm"][2] == pytest.approx(6 * 2.54)
    assert rat["startCm"] == row["startCm"]


def test_no_npc_holds_the_jump_capability_so_nothing_is_usable():
    assert jump.JUMP_CAPABILITY_HELD is False
    assert not any(jump.capability_usable(word) for word in range(16))
    summary = jump.project_graph(_two_hull_graph(2), "fixture")["summary"]
    assert summary["graph"] == {"0": dict(jumpOnly=1, groundAndJump=0, usable=0),
                                "19": dict(jumpOnly=1, groundAndJump=0, usable=0)}
    assert summary["recorded"] == {"0": dict(jumpOnly=1, usable=0), "19": dict(jumpOnly=1, usable=0)}


def test_a_verdict_change_invalidates_the_level_recipe():
    first = jump.project_graph(_two_hull_graph(1), "fixture")["sha256"]
    assert jump.project_graph(_two_hull_graph(2), "fixture")["sha256"] != first


@pytest.mark.parametrize("map_name,human,rat,recorded_rat", [
    ("sp_tutorial_1", 25, 36, 17),
    ("sm_hub_1", 117, 103, 68),
])
def test_the_witness_graphs_per_hull_jump_counts_and_none_usable(map_name, human, rat, recorded_rat):
    payload = _staged(map_name)
    summary = payload["summary"]
    assert payload["usedHulls"] == [0, 19]
    assert summary["links"] == len(payload["links"]) == human
    assert summary["graph"] == {"0": dict(jumpOnly=human, groundAndJump=0, usable=0),
                                "19": dict(jumpOnly=rat, groundAndJump=0, usable=0)}
    assert summary["recorded"] == {"0": dict(jumpOnly=human, usable=0),
                                   "19": dict(jumpOnly=recorded_rat, usable=0)}
    assert all(row["bidirectional"] for row in payload["links"])


def _answer(index, **overrides):
    row = dict(index=index, src=0, dst=1, enabled=False, agentBits=0, traversable=False,
               verdicts=[dict(hull=0, motion=2, jumpOnly=True, capabilityUsable=False,
                              legalForward=True, legalBack=False)])
    row.update(overrides)
    return row


def test_the_gate_passes_present_disabled_records():
    row = nav_gate.baked_jump_link_errors({"expected": [4, 7]}, [_answer(4), _answer(7)])
    assert row["failed"] == 0 and row["present"] == row["disabled"] == 2
    assert row["verdictsByHull"] == {"0": dict(records=2, jumpOnly=2, usable=0, legalForward=2,
                                               legalBack=0)}


@pytest.mark.parametrize("override", [dict(enabled=True), dict(agentBits=1), dict(traversable=True)])
def test_any_traversable_jump_link_fails_the_gate(override):
    row = nav_gate.baked_jump_link_errors({"expected": [4]}, [_answer(4, **override)])
    assert row["failed"] == 1 and "0x102ff960" in row["failures"][0]["reason"]


def test_a_missing_an_extra_and_a_doubled_record_each_fail():
    row = nav_gate.baked_jump_link_errors(
        {"expected": [4, 7]}, [_answer(4), _answer(4), _answer(9)])
    reasons = sorted(failure["reason"] for failure in row["failures"])
    assert row["failed"] == 3
    assert reasons == ["a jump-link record the key does not stage",
                       "the jump link is recorded more than once",
                       "the staged jump link has no record"]


def test_a_stale_verify_script_with_no_records_fails_rather_than_passing():
    assert nav_gate.baked_jump_link_errors({"expected": []}, None)["failed"] == 1


def _jump_row(index, motion=2):
    return dict(index=index, src=index, dst=index + 1, straightCm=630.0, motion=motion)


def test_the_jump_only_pairs_the_mesh_walks_are_reported_never_failed():
    rows = [_jump_row(731), _jump_row(5), _jump_row(6)]
    row = nav_gate.jump_reach_report(rows, [640.0, -1.0, -2.0], 0)
    assert row["failed"] == 0 and row["report"]
    assert (row["jumpOnly"], row["walked"], row["noPath"], row["offMesh"]) == (3, 1, 1, 1)
    assert row["walkedIds"] == [731] and row["walkedPairs"][0]["pathCm"] == 640.0


def test_a_ground_and_jump_word_is_not_a_jump_only_pair():
    row = nav_gate.jump_reach_report([_jump_row(1, motion=3)], [100.0], 0)
    assert row["jumpOnly"] == 0 and row["walked"] == 0


def test_an_unasked_hull_says_so_and_does_not_fail():
    row = nav_gate.jump_reach_report([_jump_row(1)], None, 0)
    assert row["failed"] == 0 and "unavailable" in row


def test_the_answer_keys_jump_rows_carry_their_word():
    from elysium_pipeline.importers import map_nav_acceptance as key
    graph = _two_hull_graph(2)
    key_rows = key.project(graph, "fixture")["perHull"]
    assert [row["motion"] for row in key_rows["0"]["jump"]] == [2]
    assert [row["motion"] for row in key_rows["19"]["jump"]] == [2]
