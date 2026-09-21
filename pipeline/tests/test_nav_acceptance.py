"""Retail's graph as an answer key, and the verdicts drawn from it.

Everything here is arithmetic over two dictionaries -- what the graph states and what Recast
answered -- which is exactly why the judgement lives outside the editor: these rules are the part
worth testing, and none of them needs a `.umap` open to be tested.
"""

import pytest

from elysium_pipeline.importers import map_nav_acceptance as key
from elysium_pipeline.validation import nav_acceptance as verdicts


def _link(index, src, dst, motions):
    fields = [0] + [0] * key.RETAIL_HULL_COUNT
    for hull, motion in motions.items():
        fields[1 + hull] = motion
    return {"index": index, "src": src, "dst": dst, "fields": fields}


def _node(index, z=0.0):
    return {"index": index, "origin": {"source": [index * 100.0, 0.0, z]},
            "hullOffsets": [0.0] * key.RETAIL_HULL_COUNT, "tail": [key.NODE_GROUND]}


def _block(links, nodes, used=(1 << 0) | (1 << 19)):
    return {"header": {"version": 30, "numHulls": key.RETAIL_HULL_COUNT,
                       "usedHullBits": {"value": used}},
            "nodes": nodes, "links": links}


def test_a_link_only_the_rat_has_is_agent_only():
    block = _block([_link(0, 0, 1, {19: key.MOVE_GROUND})], [_node(0), _node(1)])
    projected = key.project(block, "probe")
    assert [row["index"] for row in projected["agentOnly"]["19"]["only"]] == [0]


def test_a_link_both_hulls_have_is_not():
    block = _block([_link(0, 0, 1, {0: key.MOVE_GROUND, 19: key.MOVE_GROUND})],
                   [_node(0), _node(1)])
    assert key.project(block, "probe")["agentOnly"]["19"]["only"] == []


def test_bridging_is_agent_only_AND_unjoined_by_the_base_hull():
    # Two rat links between the same pair. One is bridging; add a human route and it stops being
    # one, because the human already joins those ends by another way.
    nodes = [_node(index) for index in range(3)]
    lone = _block([_link(0, 0, 2, {19: key.MOVE_GROUND})], nodes)
    assert len(key.project(lone, "probe")["agentOnly"]["19"]["bridging"]) == 1

    joined = _block([_link(0, 0, 2, {19: key.MOVE_GROUND}),
                     _link(1, 0, 1, {0: key.MOVE_GROUND}),
                     _link(2, 1, 2, {0: key.MOVE_GROUND})], nodes)
    projected = key.project(joined, "probe")
    assert projected["agentOnly"]["19"]["only"]      # still the rat's alone
    assert projected["agentOnly"]["19"]["bridging"] == []   # but no longer a bridge


def test_bridging_rows_carry_both_hulls_endpoints():
    # A bridging link has NO row in the base hull's table, so its base endpoints have to come from
    # the key. Looking them up in that table instead compares a degenerate segment and indicts a
    # mesh that is fine -- which is exactly what happened the first time this ran.
    block = _block([_link(0, 0, 1, {19: key.MOVE_GROUND})], [_node(0), _node(1)])
    row = key.project(block, "probe")["agentOnly"]["19"]["bridging"][0]
    assert {"startCm", "endCm", "baseStartCm", "baseEndCm"} <= set(row)


def test_a_rise_between_the_two_step_heights_is_an_outlier():
    # The graph was laid by CAI_TestHull stepping 40 units; NPCs step 18. A link asserting a rise
    # between the two is unreachable for a real agent and is retail's own inconsistency.
    block = _block([_link(0, 0, 1, {0: key.MOVE_GROUND})], [_node(0), _node(1, z=30.0)])
    outliers = key.project(block, "probe")["stepOutliers"]
    assert [row["index"] for row in outliers] == [0]


def test_a_rise_an_npc_can_climb_is_not():
    block = _block([_link(0, 0, 1, {0: key.MOVE_GROUND})], [_node(0), _node(1, z=10.0)])
    assert key.project(block, "probe")["stepOutliers"] == []


def test_a_rise_above_even_the_graph_builder_is_not_excused():
    # Above 40 the graph itself could not have walked it either, so it is not the known
    # inconsistency -- it is something else, and excusing it would hide it.
    block = _block([_link(0, 0, 1, {0: key.MOVE_GROUND})], [_node(0), _node(1, z=90.0)])
    assert key.project(block, "probe")["stepOutliers"] == []


def test_a_hole_and_a_wall_are_different_findings():
    links = [{"index": 1, "src": 0, "dst": 1, "straightCm": 100.0},
             {"index": 2, "src": 1, "dst": 2, "straightCm": 100.0}]
    row = verdicts.ground_link_errors(links, [verdicts.OFF_MESH, verdicts.NO_PATH], hull=0)
    assert (row["holes"], row["walls"], row["detours"]) == (1, 1, 0)


def test_a_route_within_the_factor_is_not_a_detour():
    links = [{"index": 1, "src": 0, "dst": 1, "straightCm": 100.0}]
    assert verdicts.ground_link_errors(links, [250.0], hull=0, factor=3.0)["failed"] == 0
    assert verdicts.ground_link_errors(links, [350.0], hull=0, factor=3.0)["detours"] == 1


def test_an_excused_link_is_not_failed():
    links = [{"index": 7, "src": 0, "dst": 1, "straightCm": 100.0}]
    assert verdicts.ground_link_errors(links, [verdicts.NO_PATH], hull=0,
                                       excused=[7])["failed"] == 0


def test_a_bridging_link_must_path_on_its_own_agents_mesh():
    bridging = [{"index": 1, "src": 0, "dst": 1}]
    assert verdicts.bridging_errors(bridging, [verdicts.NO_PATH], [verdicts.NO_PATH],
                                    hull=19, base_hull=0)["missing"] == 1
    assert verdicts.bridging_errors(bridging, [500.0], [verdicts.NO_PATH],
                                    hull=19, base_hull=0)["failed"] == 0


def test_the_base_mesh_reaching_it_too_is_reported_not_failed():
    # Measured 2026-09-20: the human mesh paths all fourteen bridging links, 70 to 9,155 cm. That
    # is the spec's own named modernization -- "NPCs can reach floor retail's sparse graph never
    # covered" -- so it is reach that changed, not a defect. Demanding Recast reproduce the
    # graph's connectivity would be demanding it reproduce the graph's sparseness.
    bridging = [{"index": 1, "src": 0, "dst": 1}]
    row = verdicts.bridging_errors(bridging, [500.0], [80.9], hull=19, base_hull=0)
    assert row["failed"] == 0
    assert row["alsoReachedByBase"] == 1
    assert row["baseReach"][0]["pathCm"] == 80.9


def test_a_mesh_with_no_tiles_is_not_a_mesh():
    # An empty mesh saves, loads and reads as "already built" exactly like a full one.
    assert verdicts.mesh_errors(["Human"], ["Human=0"])["failed"] == 1
    assert verdicts.mesh_errors(["Human"], ["Human=915"])["failed"] == 0


def test_a_missing_agent_and_an_unasked_one_both_count():
    assert verdicts.mesh_errors(["Human", "Rat"], ["Human=915"])["failed"] == 1
    assert verdicts.mesh_errors(["Human"], ["Human=915", "Werewolf=12"])["failed"] == 1


def test_agent_names_follow_the_generated_ini_rule():
    assert key.agent_name("HUMAN_HULL") == "Human"
    assert key.agent_name("MING_XIAO_PATHING_HULL") == "MingXiaoPathing"
    assert key.agent_name("RAT_HULL") == "Rat"


def test_a_pinned_finding_that_reproduces_is_reported_not_failed():
    links = [{"index": 7, "src": 0, "dst": 1, "straightCm": 100.0}]
    row = verdicts.ground_link_errors(links, [450.0], hull=19, known={7: "rat hole"})
    assert row["failed"] == 0
    assert [finding["index"] for finding in row["knownFindings"]] == [7]


def test_a_new_finding_beside_a_pinned_one_still_fails():
    links = [{"index": 7, "src": 0, "dst": 1, "straightCm": 100.0},
             {"index": 8, "src": 1, "dst": 2, "straightCm": 100.0}]
    row = verdicts.ground_link_errors(links, [450.0, 450.0], hull=19, known={7: "rat hole"})
    assert row["failed"] == 1
    assert row["failures"][0]["index"] == 8


def test_a_pin_that_stops_reproducing_fails_until_it_is_removed():
    # A pin is not a tolerance: once the finding is gone the pin has to go too, or it would
    # quietly excuse the next regression on that link.
    links = [{"index": 7, "src": 0, "dst": 1, "straightCm": 100.0}]
    row = verdicts.ground_link_errors(links, [120.0], hull=19, known={7: "rat hole"})
    assert row["failed"] == 1 and row["stalePins"] == 1


def test_the_pinned_detours_are_the_three_hub_rat_holes():
    from elysium_pipeline.validation.nav_known_findings import known_detours
    assert sorted(known_detours("sm_hub_1", 19)) == [406, 721, 1472]
    assert known_detours("sp_tutorial_1", 19) == {}


def test_the_theatres_five_pins_are_the_one_dropped_node():
    """0018 story 21-2: five links, one destination, one 132 cm drop. Pinned on the human hull --
    `sp_theatre`'s graph is human-only, so there is no rat hull to pin anything on."""
    from elysium_pipeline.validation.nav_known_findings import known_detours
    pins = known_detours("sp_theatre", 0)
    assert sorted(pins) == [4, 8, 10, 53, 56]
    assert all("132 cm down onto node 26" in why for why in pins.values())
    assert known_detours("sp_theatre", 19) == {}
