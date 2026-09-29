"""The place set (0018 story 4): retail's AIN nodes, bound to their hints by `CNodeEnt::Spawn`'s
positional counter (`0x102d78d0`), plus the crosswalk pairs and the per-hull wander caps.

The synthetic cases pin the counter rule arm by arm; the export-gated cases (skipped without a V2
export root holding both units) pin the two witness maps.
"""
from __future__ import annotations

import pytest

from elysium_pipeline import paths
from elysium_pipeline.formats.nav_graph_glb.model import NODE_GROUND
from elysium_pipeline.importers import map_places as places

HULLS = places.RETAIL_HULL_COUNT


def _row(index, classname, **keys):
    pairs = [("classname", classname), *((key, str(value)) for key, value in keys.items())]
    return {"index": index, "classname": classname, "outputs": [],
            "keyValues": [{"index": i, "key": key.lower(), "sourceKey": key, "value": value}
                          for i, (key, value) in enumerate(pairs)]}


def _node(index, x=0.0, wc_id=None, yaw=0.0):
    return {"index": index, "origin": {"source": [x, 10.0, 20.0]}, "yaw": yaw,
            "hullOffsets": [-3.87] * HULLS, "type": 2, "flags": 0,
            "wcId": index + 1 if wc_id is None else wc_id}


def _link(index, src, dst, **motions):
    fields = [0] + [0] * HULLS
    for hull, motion in motions.items():
        fields[1 + int(hull[1:])] = motion
    return {"index": index, "src": src, "dst": dst, "fields": fields}


def _block(num_nodes, links=(), used=1):
    return {"header": {"numNodes": num_nodes, "usedHullBits": {"value": used}},
            "nodes": [_node(i, x=100.0 * i) for i in range(num_nodes)], "links": list(links)}


def _stage(rows, block):
    return places.stage_rows("synthetic", rows, block)


def _hints(payload):
    return [place["hint"] for place in payload["places"]]


# --- the counter ----------------------------------------------------------------------------------


def test_standalone_hints_never_advance_the_counter():
    rows = [_row(0, "worldspawn"),
            _row(1, "info_hint", hinttype=100),
            _row(2, "info_node_kick_over"),          # class-forced 10300: a hint, id -1
            _row(3, "info_node_patrol_point", hinttype=10000, Group="A1"),
            _row(4, "info_node_shoot_at"),
            _row(5, "info_node_patrol_point", hinttype=10000, Group="A2")]
    payload = _stage(rows, _block(2))
    assert _hints(payload) == [3, 5]
    assert payload["pairing"] == {"nodeRows": 2, "outOfRange": [], "standalone": [1, 2, 4]}


def test_a_type_0_standalone_makes_nothing_and_counts_nothing():
    rows = [_row(0, "info_hint"), _row(1, "info_hint", hinttype=0), _row(2, "info_node_hint",
                                                                         hinttype=5)]
    payload = _stage(rows, _block(1))
    assert _hints(payload) == [2]
    assert payload["pairing"]["standalone"] == []
    assert payload["pairing"]["nodeRows"] == 1


def test_a_node_row_with_no_hint_still_advances_the_counter():
    rows = [_row(0, "info_node"),                         # no type, no Group: no hint, advances
            _row(1, "info_node_tzimisce"),                # renamed info_node first: the same
            _row(2, "info_node_cover_low"),               # class-forced 101: a hint on node 2
            _row(3, "info_node", Group="walkers")]        # a Group alone makes one
    payload = _stage(rows, _block(4))
    assert _hints(payload) == [-1, -1, 2, 3]
    assert payload["pairing"]["nodeRows"] == 4


def test_a_hint_past_the_last_node_is_out_of_range_and_the_rows_keep_counting():
    rows = [_row(0, "info_node_patrol_point", hinttype=10000),
            _row(1, "info_node"),
            _row(2, "info_node_patrol_point", hinttype=10000),
            _row(3, "info_node"),                         # past the end, no hint: nothing to record
            _row(4, "info_node_crosswalk")]
    payload = _stage(rows, _block(2))
    assert _hints(payload) == [0, -1]
    assert payload["pairing"] == {
        "nodeRows": 5, "outOfRange": [{"bspIndex": 2, "counter": 2},
                                      {"bspIndex": 4, "counter": 4}],
        "standalone": []}


def test_the_binding_is_positional_whatever_the_authored_nodeid_says():
    # Both rows claim nodeid 7; the counter binds them to nodes 0 and 1 all the same, and the
    # report counts the rows whose nodeid is not their node's wcId.
    rows = [_row(0, "info_node_patrol_point", hinttype=10000, nodeid=7),
            _row(1, "info_node_patrol_point", hinttype=10000, nodeid=7)]
    block = _block(2)
    block["nodes"][0]["wcId"] = 7
    block["nodes"][1]["wcId"] = 8
    payload = _stage(rows, block)
    assert _hints(payload) == [0, 1]
    assert [place["wcId"] for place in payload["places"]] == [7, 8]
    assert places.pairing_report(payload, rows) == {"paired": 2, "agree": 1, "differ": 1,
                                                    "noNodeid": 0}


def test_a_parented_node_row_is_refused():
    rows = [_row(0, "info_node", parentname="train")]
    with pytest.raises(places.MapPlacesError, match="parented"):
        _stage(rows, _block(1))


# --- the row --------------------------------------------------------------------------------------


def test_a_place_row_is_unreal_native_with_every_hull_offset_and_no_offset_applied():
    block = _block(1)
    block["nodes"][0].update(yaw=90.0, wcId=None)
    (row,) = _stage([], block)["places"]
    assert row["origin"] == [0.0, -25.4, 50.8]        # raw origin, Y reflected, no Z offset
    assert row["yaw"] == -90.0
    assert row["zOffsets"] == [round(-3.87 * 2.54, 5)] * HULLS
    assert (row["type"], row["flags"], row["wcId"], row["hint"]) == (2, 0, -1, -1)
    assert set(row) == {"index", "type", "flags", "origin", "yaw", "zOffsets", "wcId", "hint"}


def test_the_block_carries_exactly_the_schema_the_reader_codes_against():
    payload = _stage([], _block(1))
    assert set(payload) == {"version", "map", "numNodes", "usedHullBits", "places", "pairing",
                            "crosswalkPairs", "wanderCaps"}
    assert payload["version"] == 1 and payload["numNodes"] == 1


# --- crosswalks and caps --------------------------------------------------------------------------


def test_crosswalk_pairs_are_links_between_two_crosswalk_bound_nodes():
    rows = [_row(0, "info_node_crosswalk"), _row(1, "info_node_crosswalk"),
            _row(2, "info_node_patrol_point", hinttype=10000), _row(3, "info_node_crosswalk")]
    links = [_link(0, 1, 0, h0=1), _link(1, 0, 1, h0=1), _link(2, 1, 2, h0=1),
             _link(3, 3, 1, h0=1)]
    payload = _stage(rows, _block(4, links))
    assert payload["crosswalkPairs"] == [[0, 1], [1, 3]]


def test_the_wander_cap_is_twenty_median_hops_per_declared_hull():
    # Human links 100, 200 and 300 long (median 200); the rat's one link is 100 long; hull 7 is
    # declared with no link of its own and borrows the human figure.
    links = [_link(0, 0, 1, h0=1, h19=1), _link(1, 1, 3, h0=1), _link(2, 0, 3, h0=2)]
    payload = _stage([], _block(4, links, used=(1 << 0) | (1 << 7) | (1 << 19)))
    assert payload["wanderCaps"] == [
        {"hull": 0, "capUnits": 4000.0, "fromHuman": False},
        {"hull": 7, "capUnits": 4000.0, "fromHuman": True},
        {"hull": 19, "capUnits": 2000.0, "fromHuman": False},
    ]


def test_an_empty_graph_stages_every_hint_out_of_range():
    rows = [_row(0, "info_node_patrol_point", hinttype=10000)]
    payload = _stage(rows, _block(0))
    assert payload["places"] == [] and payload["crosswalkPairs"] == []
    assert payload["pairing"]["outOfRange"] == [{"bspIndex": 0, "counter": 0}]
    assert payload["wanderCaps"] == [{"hull": 0, "capUnits": 0.0, "fromHuman": True}]


# --- the witnesses --------------------------------------------------------------------------------


def _export_root():
    try:
        root = paths.export_v2_root()
    except RuntimeError:
        pytest.skip("V2 export root not configured")
    return root


def _witness(map_name):
    root = _export_root()
    if not (root / "maps" / f"{map_name}.entities.glb").is_file():
        pytest.skip(f"{map_name} entity unit unavailable")
    if not (root / "nav-graphs" / f"{map_name}.glb").is_file():
        pytest.skip(f"{map_name} nav-graph unit unavailable")
    from elysium_pipeline.exporters import UE_map_sidecars as producer
    rows = producer.read_units(map_name, root).entities["entities"]
    return places.stage_map(map_name, root), rows


@pytest.mark.parametrize("map_name, nodes, human_cap, paired", [
    ("sp_tutorial_1", 203, 2942, 49),
    ("sm_hub_1", 578, 3113, 274),
])
def test_the_witness_place_sets(map_name, nodes, human_cap, paired):
    payload, rows = _witness(map_name)
    assert payload["numNodes"] == len(payload["places"]) == nodes
    assert {place["type"] for place in payload["places"]} == {NODE_GROUND}
    assert payload["usedHullBits"] == 0x80001
    # Every node row advanced the counter onto a node of its own: the patch AINs were built from
    # the patch BSPs, so the positional binding and the authored ids agree throughout.
    assert payload["pairing"] == {"nodeRows": nodes, "outOfRange": [], "standalone": []}
    assert places.pairing_report(payload, rows) == {
        "paired": paired, "agree": paired, "differ": 0, "noNodeid": 0}
    caps = {cap["hull"]: cap for cap in payload["wanderCaps"]}
    assert sorted(caps) == [0, 19]
    assert round(caps[0]["capUnits"]) == human_cap and not caps[0]["fromHuman"]


def test_the_thugs_patrol_points_are_bound_to_their_nodes():
    payload, rows = _witness("sp_tutorial_1")
    by_hint = {place["hint"]: place for place in payload["places"] if place["hint"] >= 0}
    for bsp_index, group, nodeid in ((433, "A1", 39), (434, "A2", 40), (435, "A3", 41)):
        keys = {kv["sourceKey"]: kv["value"] for kv in rows[bsp_index]["keyValues"]}
        assert rows[bsp_index]["classname"] == "info_node_patrol_point"
        assert (keys["Group"], keys["nodeid"]) == (group, str(nodeid))
        assert by_hint[bsp_index]["wcId"] == nodeid


def test_the_hub_crosswalk_nodes_pair_with_each_other():
    payload, rows = _witness("sm_hub_1")
    crosswalk_rows = {index for index, row in enumerate(rows)
                      if row["classname"].lower() == "info_node_crosswalk"}
    assert len(crosswalk_rows) == 6
    crosswalk_nodes = {place["index"] for place in payload["places"]
                       if place["hint"] in crosswalk_rows}
    assert len(crosswalk_nodes) == 6
    pairs = payload["crosswalkPairs"]
    assert pairs and {node for pair in pairs for node in pair} == crosswalk_nodes
    assert all(a < b for a, b in pairs) and len({tuple(pair) for pair in pairs}) == len(pairs)
