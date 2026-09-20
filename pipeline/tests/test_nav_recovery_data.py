"""The two recovered data files 0018 story 3 builds its world on.

`docs/vtmb/data/hull_table.json` (the 22 `NAI_Hull` rows) and
`research/tooling/data/contents_masks.json` (the four trace masks a brush answers) are
transcriptions of the retail image. The probes that produced them replay the DLL, so the tests
that need it skip without the install; everything provable from the committed bytes alone --
shape, self-consistency, and the invariants the oracle states -- is asserted unconditionally.

The mask rows carry each value twice, decimal and hex. That redundancy earned itself: a
hand-typed decimal that disagreed with its hex silently deleted a whole signature from the
census and invented one retail does not ship.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "probes"))

HULL_TABLE = REPO / "docs" / "vtmb" / "data" / "hull_table.json"
CONTENTS_MASKS = REPO / "research" / "tooling" / "data" / "contents_masks.json"

#: Retail's hull count, asserted by the `.ain` loader `0x102f5bd0` on every graph it reads.
EXPECTED_HULLS = 22

#: The 14 rows carrying links in a shipped graph, by bit -- the agents the bake generates.
LINK_CARRYING_BITS = (0, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21)


def _load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _have_install() -> bool:
    root = os.environ.get("ELYSIUM_VTMB_ROOT")
    return bool(root) and Path(root).is_dir()


# ---------------------------------------------------------------- the hull table

def test_hull_table_has_every_retail_row_in_bit_order():
    rows = _load(HULL_TABLE)["rows"]
    assert len(rows) == EXPECTED_HULLS
    assert [row["bit"] for row in rows] == list(range(EXPECTED_HULLS))
    # A row's index IS its bit: the mask is 1 << index, which is what `UsedHullBits` is an OR of.
    assert [row["mask"] for row in rows] == [1 << index for index in range(EXPECTED_HULLS)]


def test_hull_table_rows_are_named_and_fully_filled():
    for row in _load(HULL_TABLE)["rows"]:
        assert row["name"], f"hull {row['bit']} has no name"
        assert row["name"].endswith("_HULL")
        for field in ("mins", "maxs", "smallMins", "smallMaxs"):
            values = row[field]
            assert len(values) == 3 and all(isinstance(v, float) for v in values), \
                f"hull {row['bit']} {field} is {values}"


def test_hull_table_extents_are_ordered_on_every_axis():
    for row in _load(HULL_TABLE)["rows"]:
        for low, high in (("mins", "maxs"), ("smallMins", "smallMaxs")):
            assert all(a <= b for a, b in zip(row[low], row[high])), \
                f"hull {row['bit']} has {low} above {high}"


def test_the_agent_hulls_are_symmetric_in_x_and_y():
    """An agent is a radius, so a row that becomes one must be symmetric about its origin.

    `WIDE_HUMAN_HULL` (bit 3) is the table's one asymmetric row -- maxs.x 20 against mins.x -15 --
    and it carries no link in any shipped graph, so no agent is ever cut from it.
    """

    rows = {row["bit"]: row for row in _load(HULL_TABLE)["rows"]}
    for bit in LINK_CARRYING_BITS:
        row = rows[bit]
        assert row["mins"][0] == -row["maxs"][0], f"{row['name']} is asymmetric in x"
        assert row["mins"][1] == -row["maxs"][1], f"{row['name']} is asymmetric in y"
    wide_human = rows[3]
    assert wide_human["name"] == "WIDE_HUMAN_HULL"
    assert wide_human["maxs"][0] != -wide_human["mins"][0]


def test_the_witness_hulls_read_as_the_oracle_states():
    rows = {row["name"]: row for row in _load(HULL_TABLE)["rows"]}
    human, rat = rows["HUMAN_HULL"], rows["RAT_HULL"]
    assert (human["mins"], human["maxs"]) == ([-13.0, -13.0, 0.0], [13.0, 13.0, 72.0])
    assert (human["smallMins"], human["smallMaxs"]) == ([-8.0, -8.0, 0.0], [8.0, 8.0, 72.0])
    assert (rat["mins"], rat["maxs"]) == ([-6.0, -6.0, 0.0], [6.0, 6.0, 10.0])


def test_two_small_hulls_are_larger_than_their_full_ones():
    """Not a transcription slip: both Tzimisce rows really do widen in the small table."""

    rows = {row["name"]: row for row in _load(HULL_TABLE)["rows"]}
    for name in ("TZIMISCE1_HULL", "TZIMISCE2_HULL"):
        assert rows[name]["smallMaxs"][0] > rows[name]["maxs"][0]


@pytest.mark.skipif(not _have_install(), reason="VtMB install not configured")
def test_hull_table_still_matches_the_image():
    import hull_table

    assert hull_table.main(["--check"]) == 0


# ---------------------------------------------------------------- the contents masks

def test_every_mask_states_the_same_value_twice():
    for row in _load(CONTENTS_MASKS)["masks"]:
        assert row["mask"] == int(row["hex"], 16), \
            f"mask {row['key']}: {row['mask']} != {row['hex']}"


def test_the_masks_are_the_four_questions_in_signature_order():
    masks = _load(CONTENTS_MASKS)["masks"]
    assert [row["key"] for row in masks] == ["player", "npc", "sight", "pedestrian"]
    assert "".join(row["letter"] for row in masks) == "PNSp"
    for row in masks:
        assert row["retail"], f"mask {row['key']} cites no retail site"


def test_the_masks_carry_the_bits_the_oracle_names():
    masks = {row["key"]: row["mask"] for row in _load(CONTENTS_MASKS)["masks"]}
    playerclip, monsterclip, moveable, opaque, solid = 0x10000, 0x20000, 0x4000, 0x80, 0x1
    # The two clip bits are what separate the player's question from the NPC's.
    assert masks["player"] & playerclip and not masks["player"] & monsterclip
    assert masks["npc"] & monsterclip and not masks["npc"] & playerclip
    # Both movement masks hit movers, which is why a door answers for both pawns.
    assert masks["player"] & moveable and masks["npc"] & moveable
    # Sight is stopped by OPAQUE even with no SOLID, and is NOT stopped by glass or grating.
    assert masks["sight"] & opaque and masks["sight"] & solid
    assert not masks["sight"] & 0x2 and not masks["sight"] & 0x8
    # The pedestrian bit is Troika's own and stands alone.
    assert masks["pedestrian"] == 0x2000


def test_the_slope_term_is_recorded_as_having_no_ai_counterpart():
    """Retail's AI never asks; the value is the player layer's standable normal."""

    slope = _load(CONTENTS_MASKS)["slope"]
    assert slope["standableNormalZ"] == 0.7
    assert slope["retail"] == "0x104492d0"
    assert slope["port"] == "ElysiumMove::StandableZ"


def test_the_graph_was_built_with_a_step_no_npc_walks():
    """The one step-height fact the acceptance harness has to be built around.

    `CAI_TestHull` lays the links down stepping 40 units; the NPCs that use them step 18. A link
    the graph asserts can therefore be unreachable on a mesh cut for a real agent, which is an
    expected outlier rather than a defect.
    """

    step = _load(CONTENTS_MASKS)["stepHeight"]
    assert step["base"]["value"] == 18.0 == step["baseSourceUnits"]
    assert step["graphBuildProbe"]["value"] == 40.0
    assert step["graphBuildProbe"]["value"] > 2 * step["base"]["value"]
    assert {row["class"]: row["value"] for row in step["overrides"]} == {
        "CNPC_VMingXiao": 30.0, "CNPC_VMingXiaoTentacle": 9.0, "CNPC_VTzimisce": 26.0}


def test_the_ground_move_clamp_constants():
    clamp = _load(CONTENTS_MASKS)["stepHeight"]["groundMoveClamp"]
    assert clamp["constants"] == {"0x104454d0": 0.5, "0x104493d0": 0.1}
    # 36.0 on hull 0: half of 72 beats 18 + 0.1.
    assert max(72.0 * clamp["constants"]["0x104454d0"],
               18.0 + clamp["constants"]["0x104493d0"]) == 36.0


# ---------------------------------------------------------------- the graphs retail loads

@pytest.mark.skipif(not _have_install(), reason="VtMB install not configured")
def test_the_witness_graphs_are_the_patch_loose_ones():
    """The pins are the patch's graphs (203/429, 578/1,862), not the packed 116/234."""

    import census_links_hulls

    directory = census_links_hulls.graphs_dir()
    tutorial = census_links_hulls.read_graph(f"{directory}/sp_tutorial_1.ain")
    hub = census_links_hulls.read_graph(f"{directory}/sm_hub_1.ain")
    assert (len(tutorial.nodes), len(tutorial.links)) == (203, 429)
    assert (len(hub.nodes), len(hub.links)) == (578, 1862)
    for model in (tutorial, hub):
        assert model.header.used_hull_bits.value == 0x80001      # human and rat
        assert model.header.num_hulls.value == EXPECTED_HULLS


@pytest.mark.skipif(not _have_install(), reason="VtMB install not configured")
def test_the_rat_links_that_bridge_separate_human_node_sets():
    """Story 3's one-sided acceptance: these must path on the rat mesh and not the human one."""

    import census_links_hulls

    directory = census_links_hulls.graphs_dir()
    tutorial = census_links_hulls.rat_only(
        census_links_hulls.read_graph(f"{directory}/sp_tutorial_1.ain"))
    hub = census_links_hulls.rat_only(census_links_hulls.read_graph(f"{directory}/sm_hub_1.ain"))
    assert tutorial["ratOnly"] == 41
    assert tutorial["bridging"] == [(0, 69), (1, 69), (44, 69), (77, 113), (146, 190)]
    assert hub["ratOnly"] == 99
    assert hub["bridging"] == [
        (155, 304), (156, 304), (158, 304), (304, 154), (304, 577),
        (526, 568), (567, 568), (568, 527), (568, 528)]
    # Nine links, but only two places: every one touches node 304 or node 568.
    assert all(304 in pair or 568 in pair for pair in hub["bridging"])


@pytest.mark.skipif(not _have_install(), reason="VtMB install not configured")
def test_the_witness_maps_carry_the_signatures_the_spec_pins():
    import contents_signatures

    masks = contents_signatures.load_masks()
    maps_dir = Path(contents_signatures.install.PATCH) / "maps"
    report = contents_signatures.census(
        [str(maps_dir / f"{name}.bsp") for name in ("sp_tutorial_1", "sm_hub_1")], masks)
    assert report["perMap"]["sp_tutorial_1"] == {"PNS-": 2725, "PN--": 309, "--S-": 17, "-N--": 5}
    assert report["perMap"]["sm_hub_1"] == {
        "PNS-": 4020, "PN--": 151, "PN-p": 93, "-N-p": 31, "---p": 9}
    # Between them the two witnesses show every signature the game ships.
    assert set(report["signatures"]) == {"PNS-", "PN--", "PN-p", "--S-", "-N-p", "-N--", "---p"}
    # No shipped brush blocks the player alone: PLAYERCLIP never ships without MONSTERCLIP.
    assert "P---" not in report["signatures"]
