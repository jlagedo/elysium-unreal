"""The pedestrian roadway mark against the `0x2000` link flag, on a two-link fixture.

Retail prices a pedestrian's step onto a `0x2000` link 5-10x (`FUN_102fe9f0`); the port marks the
same ground as `UElysiumNavArea_Pedestrian` over the `---p` brushes. The census asks the two
against each other, so these pins say what a crossing is, that a matrix row counts each side
separately, and when the census fails.
"""

import pytest

from elysium_pipeline.importers import map_collision, map_nav_doors
from elysium_pipeline.validation import pedestrian_link_census as census

HULLS = map_nav_doors.RETAIL_HULL_COUNT


def _node(index, x, y, z=0.0):
    return {"index": index, "type": 2, "origin": {"source": [x, y, z]}, "hullOffsets": [0.0] * HULLS}


def _link(index, src, dst, info, ground=1):
    fields = [info, ground] + [0] * (HULLS - 1)
    return {"index": index, "src": src, "dst": dst, "fields": fields}


def _block():
    """Nodes 0-1 span x 0..100 at y 0; nodes 2-3 span x 0..100 at y 5000, far from any box."""
    return {
        "nodes": [_node(0, 0, 0), _node(1, 100, 0), _node(2, 0, 5000), _node(3, 100, 5000)],
        "links": [
            _link(0, 0, 1, census.PEDESTRIAN_LINK_FLAG),   # flagged, across the roadway
            _link(1, 2, 3, 0),                              # unflagged ground, far away
        ],
    }


def _box_over_first_link():
    a = map_nav_doors._node_position(_block()["nodes"][0], 0)
    b = map_nav_doors._node_position(_block()["nodes"][1], 0)
    mid = [(a[i] + b[i]) / 2 for i in range(3)]
    return [mid[i] - 5 for i in range(3)], [mid[i] + 5 for i in range(3)]


def test_the_flagged_link_across_the_box_crosses_and_the_far_link_does_not():
    row = census.link_matrix("fixture", _block(), [_box_over_first_link()])
    assert (row.boxes, row.flagged, row.flagged_crossing) == (1, 1, 1)
    assert (row.unflagged_ground, row.unflagged_crossing) == (1, 0)


def test_the_box_is_grown_by_hull_zero_before_the_link_is_tested():
    # A box 20 cm beside the segment misses a bare line and hits it once grown by hull 0's 33 cm.
    lo, hi = _box_over_first_link()
    beside = ([lo[0], lo[1] + 25.0, lo[2]], [hi[0], hi[1] + 35.0, hi[2]])
    assert census.link_matrix("fixture", _block(), [beside]).flagged_crossing == 1
    assert census.link_matrix("fixture", _block(), [beside], radius_cm=0.0).flagged_crossing == 0


def test_a_jump_link_or_a_switched_off_link_is_not_an_unflagged_ground_link():
    block = _block()
    block["links"].append(_link(2, 2, 3, 0, ground=2))                    # a jump link
    block["links"].append(_link(3, 2, 3, map_nav_doors.LINK_OFF))         # refused by 0x102ff960
    row = census.link_matrix("fixture", block, [_box_over_first_link()])
    assert row.unflagged_ground == 1


def test_no_box_means_nothing_crosses():
    row = census.link_matrix("fixture", _block(), [])
    assert (row.boxes, row.flagged, row.flagged_crossing, row.unflagged_crossing) == (0, 1, 0, 0)


def test_only_the_clip_free_signature_is_a_pedestrian_box(tmp_path):
    hull = "0 0 0 1 0 0 0 1 0 0 0 1"
    path = tmp_path / "m.hulls"
    # `0x2000` alone is `---p`; `0x22000` also carries MONSTERCLIP, which `InitLinks` stops at.
    path.write_text(f"0x2000 {hull}\n0x22000 {hull}\n0x0 {hull}\n", encoding="utf-8")
    assert map_collision.PEDESTRIAN_SIGNATURE == 8
    assert census.pedestrian_boxes(path) == [([0.0, 0.0, 0.0], [1.0, 1.0, 1.0])]


def _row(name, flagged, crossing=0, unflagged_crossing=0, boxes=0):
    return census.MapMatrix(name, boxes, flagged, crossing, 10, unflagged_crossing)


def test_a_map_with_no_flagged_link_and_a_crossing_fails():
    failures = census.verdict([_row("la_x", 0, unflagged_crossing=2, boxes=1)], check_totals=False)
    assert failures and "la_x" in failures[0]


def test_a_flagged_map_with_unflagged_crossings_is_reported_not_failed():
    # `hw_hub_1` measures 124 of these; the matrix is printed, the exit status is not.
    assert census.verdict([_row("hw_hub_1", 369, 365, 124, boxes=13)], check_totals=False) == []


def test_the_flagged_totals_are_gated():
    rows = [_row("sm_hub_1", census.EXPECTED_HUB_FLAGGED)]
    failures = census.verdict(rows)
    assert any("flagged links" in failure for failure in failures)
    assert any("maps carrying" in failure for failure in failures)

    exact = [_row("sm_hub_1", census.EXPECTED_HUB_FLAGGED)]
    remaining = census.EXPECTED_FLAGGED_TOTAL - census.EXPECTED_HUB_FLAGGED
    for index in range(census.EXPECTED_FLAGGED_MAPS - 1):
        share = remaining // (census.EXPECTED_FLAGGED_MAPS - 1 - index)
        exact.append(_row(f"map_{index}", share))
        remaining -= share
    assert census.verdict(exact) == []


def test_the_published_hub_matrix():
    pytest.importorskip("elysium_pipeline.paths")
    from elysium_pipeline import paths
    try:
        root = paths.export_v2_root()
    except RuntimeError:
        pytest.skip("V2 export root not configured")
    if not (root / "nav-graphs" / "sm_hub_1.glb").is_file():
        pytest.skip("sm_hub_1 nav graph not exported")
    if not census.sidecar_hulls_path("sm_hub_1").is_file():
        pytest.skip("sm_hub_1 hulls sidecar not written")

    row = census.census_map("sm_hub_1")
    # The spec's numbers from the link side: 461 flagged against 1,185 unflagged hull-0 ground
    # links, 9 boxes, and none of the unflagged crossing one. The flagged crossing is the census'
    # own (458; the brush-side census reported 455) -- pinned as a floor, not as equality.
    assert (row.boxes, row.flagged, row.unflagged_ground, row.unflagged_crossing) == (9, 461, 1185, 0)
    assert row.flagged_crossing >= 455
