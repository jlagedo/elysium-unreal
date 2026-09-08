"""Retail AIN filtering and the native endpoint representation for requirement 19."""
from copy import deepcopy

import pytest

from elysium_pipeline import paths
from elysium_pipeline.importers import map_jump_links as jump


def _graph():
    return dict(header=dict(version=30, numHulls=22, usedHullBits=dict(value=1),
                            numNodes=2, totalNumLinks=1),
                nodes=[dict(index=i, origin=dict(source=[i * 100, 20, 30]),
                            hullOffsets=[-4.0] * 22, tail=[2]) for i in range(2)],
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
    if damage == "node_type": graph["nodes"][0]["tail"][0] = 4
    with pytest.raises(ValueError): jump.project_graph(graph, "fixture")


def test_endpoint_or_filter_change_invalidates_level_recipe():
    graph = _graph()
    first = jump.project_graph(graph, "fixture")["sha256"]
    graph["nodes"][0]["hullOffsets"][0] += 1
    assert jump.project_graph(graph, "fixture")["sha256"] != first
    graph["links"][0]["fields"][0] = jump.LINK_OFF
    assert jump.project_graph(graph, "fixture")["sha256"] != first


def test_tutorial_decoded_ain_has_nine_human_jump_connections():
    try:
        root = paths.export_v2_root()
    except RuntimeError:
        pytest.skip("V2 export root not configured")
    if not (root / "nav-graphs/sp_tutorial_1.glb").is_file():
        pytest.skip("tutorial V2 navigation unit unavailable")
    payload = jump.stage_map("sp_tutorial_1", root)
    assert payload["sourceLinks"] == 234
    assert [row["index"] for row in payload["links"]] == [22, 24, 30, 88, 110, 115, 147, 163, 218]
    assert payload["excluded"] == dict(nonJump=225, disabled=0, duplicate=0)
