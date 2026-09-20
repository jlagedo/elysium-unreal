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


def test_the_door_classnames_are_the_two_that_answer_moveable():
    assert set(doors.DOOR_CLASSNAMES) == {"func_door", "func_door_rotating"}


def test_hull_radius_is_the_tables_lateral_extent():
    # Hull 0 is 26 Source units across, so 13 out from centre -> 33.02 cm.
    assert doors.hull_radius_cm(0) == pytest.approx(33.02)
    # The rat is 12 across -> 15.24 cm.
    assert doors.hull_radius_cm(19) == pytest.approx(15.24)


@pytest.mark.parametrize("map_name, total, traversable, partial", [
    # The designers' own choice of which encounters can reach the player. Derived here from the
    # PATCH's graph; the same figures were first measured by an out-of-repo census.
    #
    # `partial` is a door one agent's links cross and another's do not -- the rat hull is not a
    # subset of the human one. On the tutorial 5 of the 8 are crossed by both, 1 by the human
    # alone and 2 by the rat alone. Job 6 leaves all 8 open on both meshes, because a nav area is
    # not per-agent; story 7's smart link is where per-agent traversal belongs.
    ("sp_tutorial_1", 36, 8, 3),
    ("sm_hub_1", 29, 2, 0),      # the smoke-shop pair, both agents
])
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
    # Only the two agents these maps build ever cross one: human 0 and rat 19.
    for row in staged["rows"]:
        assert set(row["crossedByHulls"]) <= {0, 19}, row
