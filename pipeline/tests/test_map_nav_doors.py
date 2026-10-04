"""Which doors retail's graph runs through -- the rule, and the two maps' answers.

`MOVEABLE 0x4000` is the bit a door answers, and the graph-build mask `0x2000b` is the only one of
retail's three movement masks without it. So `InitLinks` builds links straight through a standing
door while every run-time probe finds that same door solid: a door the designers gave a link is
one NPCs use, and a door with no link is a wall. Unreal inherits no such distinction, so the bake
makes it, and these are the pins that say it made it right.
"""

import math

import pytest

from elysium_pipeline.importers import map_nav_doors as doors


def test_a_segment_through_a_box_is_a_crossing():
    assert doors.segment_hits_box((-10, 0, 0), (10, 0, 0), (-1, -1, -1), (1, 1, 1))


def test_a_segment_beside_a_box_is_not():
    assert not doors.segment_hits_box((-10, 5, 0), (10, 5, 0), (-1, -1, -1), (1, 1, 1))


def test_a_segment_that_stops_short_is_not():
    # The slab test is bounded to the segment, not the infinite ray: a link that ends before the
    # doorway does not run through it.
    assert not doors.segment_hits_box((-10, 0, 0), (-5, 0, 0), (-1, -1, -1), (1, 1, 1))


def test_a_segment_wholly_inside_counts():
    assert doors.segment_hits_box((0, 0, 0), (0.5, 0, 0), (-1, -1, -1), (1, 1, 1))


def test_hull_bounds_are_moved_into_world_space_by_the_entity_origin():
    # A brush entity's hulls are staged in its OWN space. Forgetting the origin puts every door at
    # the world origin, where no link crosses any of them -- a clean-looking, wrong answer.
    hull = [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1]
    local = doors.hull_bounds_cm([hull])
    moved = doors.hull_bounds_cm([hull], (100.0, -50.0, 7.0))
    assert local == ([0, 0, 0], [1, 1, 1])
    assert moved == ([100.0, -50.0, 7.0], [101.0, -49.0, 8.0])


def test_the_link_endpoints_are_where_the_witness_enters_and_leaves_the_grown_box():
    # 0018/7: the smart link joins the two faces of the grown (cut + agent erosion) box the witness
    # link runs through -- the mesh's edges on either side of the closed leaf.
    start, end = doors.clip_link((-10, 0, 0), (10, 0, 0), (-2, -1, -1), (3, 1, 1))
    assert start == pytest.approx([-2.0, 0.0, 0.0])
    assert end == pytest.approx([3.0, 0.0, 0.0])


def test_a_witness_node_inside_the_box_keeps_its_position():
    start, end = doors.clip_link((0, 0, 0), (10, 0, 0), (-2, -1, -1), (3, 1, 1))
    assert start == pytest.approx([0.0, 0.0, 0.0])
    assert end == pytest.approx([3.0, 0.0, 0.0])


def test_a_link_that_misses_the_box_is_refused():
    with pytest.raises(ValueError):
        doors.clip_link((-10, 5, 0), (10, 5, 0), (-2, -1, -1), (3, 1, 1))


def test_one_link_per_witness_spans_every_door_it_crosses():
    # Two leaves on one AIN link (the hub's smoke-shop pair shape): ONE link, entering the first
    # leaf's grown box and leaving the second's.
    def row(index, lo_x, hi_x):
        return {"entityIndex": index, "traversable": True, "crossedByHulls": [0], "linkHull": 0,
                "linkWitness": 7, "linkSegmentCm": [[-100.0, 0.0, 0.0], [100.0, 0.0, 0.0]],
                "boundsCm": [[lo_x, -1.0, -1.0], [hi_x, 1.0, 1.0]]}
    links = doors.door_links([row(10, -5.0, -4.0), row(11, 4.0, 5.0)], lambda hull: 2.0)
    assert len(links) == 1
    assert links[0]["doors"] == [10, 11]
    assert links[0]["startCm"] == pytest.approx([-7.0, 0.0, 0.0])
    assert links[0]["endCm"] == pytest.approx([7.0, 0.0, 0.0])


def test_the_door_classnames_are_the_two_that_answer_moveable():
    assert set(doors.DOOR_CLASSNAMES) == {"func_door", "func_door_rotating"}


def test_hull_radius_is_the_tables_lateral_extent():
    # Hull 0 is 26 Source units across, so 13 out from centre -> 33.02 cm.
    assert doors.hull_radius_cm(0) == pytest.approx(33.02)
    # The rat is 12 across -> 15.24 cm.
    assert doors.hull_radius_cm(19) == pytest.approx(15.24)


def test_hull_box_is_the_tables_own_box_in_unreal_centimetres():
    # The rat: (-6, -6, 0)..(6, 6, 10) units. Y is mirrored, so the low Y bound is -maxs.y.
    mins, maxs = doors.hull_box_cm(19)
    assert mins == pytest.approx([-15.24, -15.24, 0.0])
    assert maxs == pytest.approx([15.24, 15.24, 25.4])
    mins, maxs = doors.hull_box_cm(0)
    assert mins == pytest.approx([-33.02, -33.02, 0.0])
    assert maxs == pytest.approx([33.02, 33.02, 182.88])


def _graph(z_units, hull=19):
    """One ground link for `hull`, 200 units long along X, at `z_units` (Source units)."""
    def node(index, x):
        return {"index": index, "origin": {"source": [x, 0.0, z_units]},
                "hullOffsets": [0.0] * doors.RETAIL_HULL_COUNT, "type": doors.NODE_GROUND}
    fields = [0] * (doors.RETAIL_HULL_COUNT + 1)
    fields[1 + hull] = 1
    return {"header": {"usedHullBits": {"value": 1 << hull}},
            "nodes": [node(0, -100.0), node(1, 100.0)],
            "links": [{"index": 41, "src": 0, "dst": 1, "fields": fields}]}


def _door(z_lo_cm, z_hi_cm):
    """A 10 cm thick leaf across the link, standing from `z_lo_cm` to `z_hi_cm` (world cm)."""
    hull = [-5.0, -60.0, z_lo_cm, 5.0, 60.0, z_hi_cm]
    return [{"entityIndex": 339, "classname": "func_door_rotating", "hulls": [hull]}]


def test_a_short_hull_passing_under_a_raised_door_crosses():
    # N20, `sp_tutorial_1` door 339: retail link 41 runs the rat at z -101 cm under a leaf whose
    # box starts at -84. The rat's own box is 10 units (25.4 cm) tall, so its walk sweeps up to
    # -75.6 and meets the leaf: the graph built through it for the rat as well as the human. The
    # old test grew the leaf's floor by the rat's 15.24 cm RADIUS (-99.24) and missed by 1.8 cm.
    rows = doors.crossings(_graph(-101.0 / 2.54), _door(-84.0, 201.0), {}, doors.hull_radius_cm)
    assert rows[0]["crossedByHulls"] == [19]
    assert rows[0]["witnessLinks"] == {19: 41}
    # The link is clipped laterally (the rat's radius off the leaf's faces), never refused.
    assert rows[0]["linkStartCm"][0] == pytest.approx(-5.0 - 15.24)
    assert rows[0]["linkEndCm"][0] == pytest.approx(5.0 + 15.24)
    # What the radius growth answered: a miss, which cut the doorway out of the rat's mesh.
    assert not doors.segment_hits_box(rows[0]["linkSegmentCm"][0], rows[0]["linkSegmentCm"][1],
                                      [-5.0 - 15.24, -60.0 - 15.24, -84.0 - 15.24],
                                      [5.0 + 15.24, 60.0 + 15.24, 201.0 + 15.24])


def test_a_door_above_the_hulls_head_is_not_crossed():
    # The same rat under a leaf raised past its 25.4 cm: it walks beneath, the graph's link does
    # not run through the door.
    rows = doors.crossings(_graph(-101.0 / 2.54), _door(-70.0, 201.0), {}, doors.hull_radius_cm)
    assert rows[0]["crossedByHulls"] == []
    assert rows[0]["traversable"] is False


@pytest.mark.parametrize("map_name, total, traversable, partial", [
    # The designers' own choice of which encounters can reach the player. Derived here from the
    # PATCH's graph; the same figures were first measured by an out-of-repo census.
    #
    # `partial` is a door one agent's links cross and another's do not -- the rat hull is not a
    # subset of the human one. On the tutorial 6 of the 8 are crossed by both, none by the human
    # alone and 2 by the rat alone (339 joined "both" when the test took the hull's own box, N20).
    # A nav area is not per-agent, so story 7 cuts all of them from every mesh and lays a
    # per-agent smart link through each of the 8.
    ("sp_tutorial_1", 36, 8, 2),
    ("sm_hub_1", 29, 2, 0),      # the smoke-shop pair, both agents
    # 0018 story 21-2's three, measured the first time they went through the lane. Every one is a
    # human-only map (`UsedHullBits` 0x1), so no door can be partial by agent: there is one agent.
    ("sp_soc_3", 5, 1, 0),
    ("sm_pawnshop_1", 10, 0, 0),   # a shop: no encounter's graph runs through any of its doors
    ("sp_theatre", 9, 0, 0),
])
@pytest.mark.corpus
def test_the_shipped_maps_door_answers(map_name, total, traversable, partial):
    pytest.importorskip("elysium_pipeline.paths")
    from elysium_pipeline import paths
    try:
        root = paths.export_v2_root()
    except RuntimeError:
        pytest.skip("V2 export root not configured")
    if not (root / "nav-graphs" / f"{map_name}.glb").is_file():
        pytest.skip(f"{map_name} nav graph not exported")

    import json
    # The bake's own sidecar root first (0018 story 21-4), then the legacy export tree.
    ents = root / "_sidecars" / map_name / f"{map_name}.ents"
    if not ents.is_file():
        ents = paths.export_root() / map_name / f"{map_name}.ents"
    if not ents.is_file():
        pytest.skip(f"{map_name} .ents not exported")
    entities = json.loads(ents.read_text(encoding="ascii"))["entities"]
    origins = {index: entity["origin"] for index, entity in enumerate(entities)
               if isinstance(entity.get("origin"), list) and len(entity["origin"]) == 3}
    rows = [{"entityIndex": index, "classname": entity.get("classname", ""),
             "hulls": entity.get("hulls") or []}
            for index, entity in enumerate(entities)]

    staged = doors.stage(doors.load_graph_block(map_name), rows, origins, doors.hull_radius_cm)
    assert staged["doors"] == total
    assert staged["traversable"] == traversable
    assert staged["cut"] == total - traversable
    assert staged["partialByAgent"] == partial
    # 0018/7: one smart link per witness AIN link, over every door it crosses.
    witnesses = {row["linkWitness"] for row in staged["rows"] if row["traversable"]}
    assert staged["linked"] == len(witnesses) == len(staged["links"])
    assert sorted(d for link in staged["links"] for d in link["doors"]) == sorted(
        row["entityIndex"] for row in staged["rows"] if row["traversable"])
    rows_by_door = {row["entityIndex"]: row for row in staged["rows"]}
    for link in staged["links"]:
        # Every crossing agent reaches both ends: they lie outside every crossing hull's grown box
        # of every door on the link (the largest hull's faces, 0018/7 review).
        for door in link["doors"]:
            row = rows_by_door[door]
            lo, hi = row["boundsCm"]
            for hull in row["crossedByHulls"]:
                radius = doors.hull_radius_cm(hull)
                for point in (link["startCm"], link["endCm"]):
                    inside = all(lo[a] - radius + 1e-3 < point[a] < hi[a] + radius - 1e-3
                                 for a in range(3))
                    assert not inside, (link, door, hull, point)
    # Only the two agents these maps build ever cross one: human 0 and rat 19.
    for row in staged["rows"]:
        assert set(row["crossedByHulls"]) <= {0, 19}, row
        if row["traversable"]:
            assert row["linkHull"] == max(row["crossedByHulls"],
                                          key=lambda h: (doors.hull_radius_cm(h), -h))
            assert row["linkWitness"] == row["witnessLinks"][row["linkHull"]]
            span = math.dist(row["linkStartCm"], row["linkEndCm"])
            # Inside the grown box: never longer than its diagonal (the box plus a hull radius on
            # every side), never a point.
            lo, hi = row["boundsCm"]
            radius = doors.hull_radius_cm(row["linkHull"])
            assert 20.0 < span <= math.dist(lo, hi) + 2.0 * math.sqrt(3.0) * radius + 1e-6, row
        else:
            assert "linkStartCm" not in row


@pytest.mark.parametrize("map_name, links", [
    # The tutorial's eight, by lump ordinal and the hulls that cross them (0018/7 findings § 2):
    # six both, `yetanotherfuckingdoor` and `soc_int_locked_door` rat only. `339` was human only
    # until N20: retail link 41 carries ground for both hulls through it (the rat's 10-unit box
    # reaches the raised leaf its radius did not).
    ("sp_tutorial_1", {183: [0, 19], 331: [0, 19], 339: [0, 19], 592: [0, 19], 851: [0, 19],
                       890: [19], 1545: [0, 19], 1546: [19]}),
    # The smoke-shop pair, both on link 958: `basic_smoke_door` (PUSE|LOCKED, the refusal test's
    # live door) and its hidden, inert partner `plus_smoke_door`.
    ("sm_hub_1", {2566: [0, 19], 2567: [0, 19]}),
])
@pytest.mark.corpus
def test_the_shipped_maps_door_links(map_name, links):
    # `links` here are the traversable DOORS and their hulls; the smart links are grouped below.
    pytest.importorskip("elysium_pipeline.paths")
    from elysium_pipeline import paths
    try:
        root = paths.export_v2_root()
    except RuntimeError:
        pytest.skip("V2 export root not configured")
    ents = root / "_sidecars" / map_name / f"{map_name}.ents"
    if not (root / "nav-graphs" / f"{map_name}.glb").is_file() or not ents.is_file():
        pytest.skip(f"{map_name} nav graph or sidecar .ents not exported")

    import json
    entities = json.loads(ents.read_text(encoding="ascii"))["entities"]
    origins = {index: entity["origin"] for index, entity in enumerate(entities)
               if isinstance(entity.get("origin"), list) and len(entity["origin"]) == 3}
    rows = [{"entityIndex": index, "classname": entity.get("classname", ""),
             "hulls": entity.get("hulls") or []}
            for index, entity in enumerate(entities)]
    staged = doors.stage(doors.load_graph_block(map_name), rows, origins, doors.hull_radius_cm)
    linked = {row["entityIndex"]: row["crossedByHulls"] for row in staged["rows"] if row["traversable"]}
    assert linked == links
    if map_name == "sm_hub_1":
        # Link 958 witnesses both leaves: ONE smart link over the pair.
        assert [(link["witness"], link["doors"]) for link in staged["links"]] == [(958, [2566, 2567])]
    else:
        assert len(staged["links"]) == 8
