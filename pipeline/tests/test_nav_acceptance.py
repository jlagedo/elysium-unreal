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
            "hullOffsets": [0.0] * key.RETAIL_HULL_COUNT, "type": key.NODE_GROUND}


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


def test_the_pinned_detours_are_the_two_hub_rat_holes():
    """Three at row 08; 721 stopped reproducing with 0018/7's bake (the rat routes it at 150 cm)
    and its pin was retired 2026-09-30."""
    from elysium_pipeline.validation.nav_known_findings import known_detours
    assert sorted(known_detours("sm_hub_1", 19)) == [406, 1472]
    assert known_detours("sp_tutorial_1", 19) == {}


def test_the_theatres_five_pins_are_the_one_dropped_node():
    """0018 story 21-2: five links, one destination, one 132 cm drop. Pinned on the human hull --
    `sp_theatre`'s graph is human-only, so there is no rat hull to pin anything on."""
    from elysium_pipeline.validation.nav_known_findings import known_detours
    pins = known_detours("sp_theatre", 0)
    assert sorted(pins) == [4, 8, 10, 53, 56]
    assert all("132 cm down onto node 26" in why for why in pins.values())
    assert known_detours("sp_theatre", 19) == {}


def test_flight_row_reports_a_flying_hull_rather_than_failing_it():
    """0018 story 21-8: hull 20's NPC flies, so its links are not walkable-mesh claims."""
    from elysium_pipeline.validation import nav_acceptance as verdicts

    links = [{"index": i, "src": i, "dst": i + 1, "straightCm": 100.0} for i in range(3)]
    ground = verdicts.ground_link_errors(links, [verdicts.OFF_MESH] * 3, 20)
    assert ground["failed"] == 3

    flown = verdicts.flight_row(ground, 20)
    assert flown["failed"] == 0
    assert flown["failures"] == []
    assert flown["flying"] is True
    assert flown["flightClaimCount"] == 3
    assert [row["index"] for row in flown["flightClaims"]] == [0, 1, 2]
    # Every other key the ground check stated is carried through.
    assert flown["links"] == 3 and flown["hull"] == 20
    assert flown["check"] == ground["check"]


def test_flight_row_counts_claims_past_the_listing_cap():
    """The count is the check's own `failed`, not the length of its truncated listing."""
    from elysium_pipeline.validation import nav_acceptance as verdicts

    n = verdicts.MAX_LISTED + 7
    links = [{"index": i, "src": i, "dst": i + 1, "straightCm": 100.0} for i in range(n)]
    ground = verdicts.ground_link_errors(links, [verdicts.OFF_MESH] * n, 20)
    flown = verdicts.flight_row(ground, 20)
    assert flown["flightClaimCount"] == n
    assert len(flown["flightClaims"]) == verdicts.MAX_LISTED
    assert flown["flightClaimsTruncated"] == 7


def test_manbat_is_the_only_flying_hull():
    """The evidence is the ManBat task list; no other hull has been shown to fly."""
    from elysium_pipeline.validation import nav_acceptance as verdicts

    assert verdicts.FLYING_HULLS == frozenset({20})


# --- The place reports (0018 story 4): observations, never checks -----------------------------

def _zoned(index, zone, *, links=0, node_type=key.NODE_GROUND, offset=0.0, x=None):
    offsets = [0.0] * key.RETAIL_HULL_COUNT
    offsets[0] = offset
    return {"index": index, "origin": {"source": [index * 100.0 if x is None else x, 0.0, 0.0]},
            "hullOffsets": offsets, "type": node_type, "zone": zone, "linkCount": links}


def _infra(*rows):
    return {"rows": [{"family": family, "classname": classname, "index": index,
                      "targetname": f"e{index}", "originCm": origin}
                     for family, classname, index, origin in rows]}


def _places(nodes, bound=None):
    bound = bound or {}
    return {"places": [{"index": node["index"], "hint": bound.get(node["index"], -1)}
                       for node in nodes]}


def test_a_ground_place_stands_at_its_hull_offset_and_any_other_type_at_its_origin():
    # CAI_Node::GetPosition 0x102fb0d0: the ground arm adds zoffset[hull], every other reads raw.
    nodes = [_zoned(0, 4, offset=10.0), _zoned(1, 4, offset=10.0, node_type=4)]
    queries = key.place_queries(_block([], nodes, used=1), _places(nodes), _infra(), [0])
    ground, climb = queries["perHull"]["0"]["pointsCm"]
    assert ground[2] == pytest.approx(10.0 * key.UNITS_TO_CM)
    assert climb[2] == pytest.approx(0.0)


def test_the_points_are_every_node_then_every_hint_patrol_point_and_interesting_place():
    nodes = [_zoned(0, 4)]
    infra = _infra(("hint", "info_node_patrol_point", 7, [0.0, 0.0, 0.0]),
                   ("hint", "info_hint", 8, [0.0, 0.0, 0.0]),
                   ("place", "intersting_place", 9, [0.0, 0.0, 0.0]),
                   ("npc", "npc_VHuman", 10, [0.0, 0.0, 0.0]))
    queries = key.place_queries(_block([], nodes, used=1), _places(nodes), infra, [0])
    assert [(p["kind"], p["id"]) for p in queries["points"]] == [
        ("node", 0), ("patrol", 7), ("hint", 8), ("place", 9)]
    assert len(queries["perHull"]["0"]["pointsCm"]) == 4


def test_a_paired_hint_is_covered_however_far_its_node_is():
    far = 10 * key.GOAL_NODE_BOX_UNITS * key.UNITS_TO_CM
    nodes = [_zoned(0, 4)]
    infra = _infra(("hint", "info_node_patrol_point", 7, [far, 0.0, 0.0]))
    queries = key.place_queries(_block([], nodes, used=1), _places(nodes, {0: 7}), infra, [0])
    assert queries["coverage"]["uncovered"] == []
    assert queries["coverage"]["pairedHints"] == 1


def test_an_unpaired_point_is_covered_only_by_a_node_inside_the_2048_box():
    # FUN_102f41b0: +-2048 units on every axis, inclusive, over raw node origins.
    edge = key.GOAL_NODE_BOX_UNITS * key.UNITS_TO_CM
    nodes = [_zoned(0, 4, x=0.0)]
    infra = _infra(("place", "intersting_place", 9, [edge - 1.0, 0.0, 0.0]),
                   ("place", "intersting_place", 10, [edge + 1.0, 0.0, 0.0]),
                   ("hint", "info_hint", 11, [0.0, 0.0, edge + 1.0]))
    coverage = key.place_queries(_block([], nodes, used=1), _places(nodes), infra, [0])["coverage"]
    assert [(row["kind"], row["id"]) for row in coverage["uncovered"]] == [("place", 10),
                                                                            ("hint", 11)]
    assert coverage["uncovered"][0]["nearestNode"] == 0
    assert coverage["farthestFromANode"]["id"] in (10, 11)


def test_zone_one_is_one_group_against_every_zone_and_never_asked_the_converse():
    # Isolated nodes carry zone 1 and IsConnected 0x102f48b0 answers 0 for them, so they make no
    # same-zone claim; as a group they still face every real zone.
    nodes = [_zoned(0, 1), _zoned(1, 1), _zoned(2, 4, links=1), _zoned(3, 4, links=2),
             _zoned(4, 5)]
    queries = key.place_queries(_block([], nodes, used=1), _places(nodes), _infra(), [0])
    asked = queries["perHull"]["0"]
    assert [(p["zoneA"], p["zoneB"]) for p in asked["zonePairs"]] == [(1, 4), (1, 5), (4, 5)]
    # Zone 4's anchor is its best-connected node; zone 5 is a single node and asks nothing.
    assert [(row["anchor"], row["node"]) for row in asked["sameZone"]] == [(3, 2)]


def test_zone_pair_candidates_are_nearest_first_and_prefer_fresh_nodes():
    nodes = [_zoned(0, 4, x=0.0), _zoned(1, 4, x=10.0), _zoned(2, 5, x=20.0),
             _zoned(3, 5, x=500.0)]
    queries = key.place_queries(_block([], nodes, used=1), _places(nodes), _infra(), [0])
    candidates = queries["perHull"]["0"]["zonePairs"][0]["candidates"]
    assert [(row["src"], row["dst"]) for row in candidates] == [(1, 2), (0, 2), (0, 3)]


def test_the_converse_says_whether_the_hulls_own_ground_links_join_the_pair():
    nodes = [_zoned(0, 4, links=2), _zoned(1, 4), _zoned(2, 4)]
    links = [_link(0, 0, 1, {0: key.MOVE_GROUND}), _link(1, 0, 2, {19: key.MOVE_GROUND})]
    queries = key.place_queries(_block(links, nodes), _places(nodes), _infra(), [0, 19])
    human = {row["node"]: row["groundLinked"] for row in queries["perHull"]["0"]["sameZone"]}
    rat = {row["node"]: row["groundLinked"] for row in queries["perHull"]["19"]["sameZone"]}
    assert human == {1: True, 2: False}
    assert rat == {1: False, 2: True}


def test_an_off_mesh_point_is_reported_with_the_first_wider_extent_it_lands_in():
    points = [{"kind": "node", "id": 0}, {"kind": "place", "id": 9}, {"kind": "hint", "id": 8}]
    landed = [[True, False, False], [True, False, False], [True, True, False]]
    wider = [[150.0, 150.0, 400.0], [400.0, 400.0, 800.0]]
    row = verdicts.places_off_mesh(points, landed, wider, 0, "Human")
    assert row["failed"] == 0 and row["observed"] == 2
    assert row["listed"][0]["landsWithinCm"] == [400.0, 400.0, 800.0]
    assert row["listed"][1]["landsWithinCm"] is None
    assert row["byKind"] == {"place": 1, "hint": 1} and row["beyondWidest"] == 1


def _pair(a, b, n):
    return {"zoneA": a, "zoneB": b,
            "candidates": [{"src": i, "dst": 100 + i, "straightCm": 100.0} for i in range(n)]}


def test_the_nearest_candidate_that_answers_decides_a_zone_pair():
    pairs = [_pair(4, 5, 2), _pair(4, 6, 2), _pair(5, 6, 2)]
    lengths = [verdicts.OFF_MESH, 250.0,               # skips the hole, joined on the second
               verdicts.NO_PATH, 300.0,                # the nearest answer is a wall: separate
               verdicts.OFF_MESH, verdicts.OFF_MESH]   # nothing answered
    row = verdicts.zone_pairs(pairs, lengths, 0, "Human")
    assert row["failed"] == 0
    assert (row["joined"], row["separate"], row["unanswered"]) == (1, 1, 1)
    assert row["listed"][0]["observation"] == "joined-on-mesh-but-separate-zones"
    assert row["listed"][0]["src"] == 1 and row["listed"][0]["pathCm"] == 250.0
    assert "hull sweep against props" in row["caveat"]


def test_a_same_zone_member_the_mesh_cannot_reach_is_observed_not_failed():
    rows = [{"zone": 4, "anchor": 0, "node": n, "straightCm": 10.0,
             "groundLinked": n == 1, "hullLinked": True} for n in (1, 2, 3)]
    row = verdicts.same_zone_unjoined(rows, [verdicts.NO_PATH, verdicts.NO_PATH,
                                             verdicts.OFF_MESH], 0, "Human")
    assert row["failed"] == 0 and row["observed"] == 2
    assert row["groundLinked"] == 1 and row["offMesh"] == 1


def test_the_observations_never_reach_the_gates_verdict():
    nodes = [_zoned(0, 4, links=1), _zoned(1, 4), _zoned(2, 5)]
    queries = key.place_queries(_block([], nodes, used=1), _places(nodes),
                                _infra(("place", "intersting_place", 9, [1.0e7, 0.0, 0.0])), [0])
    asked = queries["perHull"]["0"]
    answers = {"0": {"landed": [[False] * 4] * 4,
                     "zonePairLengths": [120.0] * sum(len(p["candidates"])
                                                      for p in asked["zonePairs"]),
                     "sameZoneLengths": [verdicts.NO_PATH] * len(asked["sameZone"])}}
    observations = verdicts.place_observations(queries, answers, {"0": "Human"})
    assert all(row["failed"] == 0 for row in observations["rows"])
    line = verdicts.place_summary(observations)
    assert line == ("places off mesh: Human 4; uncovered: 1; zone pairs joined on mesh: "
                    "Human 1/1; same zone, no mesh path: Human 1")


def test_a_map_whose_places_cannot_be_staged_says_so_and_still_reports():
    observations = verdicts.place_observations({"unavailable": "no entities"}, {}, {})
    assert verdicts.place_summary(observations) == "place reports unavailable (no entities)"
