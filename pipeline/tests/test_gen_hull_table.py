"""The hull-table generator: retail's extents for the kernel, Unreal's agents for the mesh.

The derivations are small and the consequences are not, so each is pinned: which hulls become
agents, how a radius is taken from a footprint, that no two agents collapse into one under the
engine's own equivalence test, and that the units cross the seam exactly once.

`FNavAgentProperties::IsEquivalent` (UE 5.8, NavigationTypes.h:483) is the reason the distinctness
test exists rather than being assumed: it calls two agents the same when radius AND height are
each within 5.0, and `RAT_HULL` against `TINY_CENTERED_HULL` clears that by 0.08 cm in both terms.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling"))

import gen_hull_table as gen  # noqa: E402

HEADER = REPO / "Source" / "ElysiumUE" / "Private" / "Substrate" / "ElysiumRetailHullTable.h"


def _rows():
    document = gen.load(gen.HULL_TABLE)
    step = gen.load(gen.MASKS_JSON)["stepHeight"]
    return document, step, gen.agents(document, step["base"]["value"])


def test_the_committed_files_match_the_hull_table():
    assert gen.main(["--check"]) == 0


def test_only_the_linked_hulls_become_agents():
    """A hull with no link is a body size: nothing could path on a mesh cut for it."""

    document, _step, rows = _rows()
    assert len(rows) == 14
    assert [row["bit"] for row in rows] == list(gen.LINKED_HULLS)
    linkless = {row["bit"] for row in document["rows"]} - set(gen.LINKED_HULLS)
    assert linkless == {1, 2, 3, 4, 5, 6, 8, 9}


def test_no_two_agents_collapse_under_the_engines_equivalence_test():
    _document, _step, rows = _rows()
    assert gen.assert_distinct(rows) == []


def test_the_rat_and_the_camera_hull_are_distinct_by_a_hair():
    """0.08 cm in both terms. If either row were rounded, the two agents would merge."""

    _document, _step, rows = _rows()
    by_name = {row["name"]: row for row in rows}
    rat, tiny = by_name["Rat"], by_name["TinyCentered"]
    radius_gap = abs(rat["radiusCm"] - tiny["radiusCm"])
    height_gap = abs(rat["heightCm"] - tiny["heightCm"])
    assert round(radius_gap, 4) == round(height_gap, 4) == 5.08
    assert radius_gap > gen.AGENT_EQUIVALENCE_PRECISION
    # Merging them would be silent: same class, same step, so only these two numbers separate them.
    assert gen.assert_distinct([rat, tiny]) == []


def test_the_agent_count_fits_the_engines_limit():
    _document, _step, rows = _rows()
    assert gen.AGENTS_MAX_COUNT == 31
    assert len(rows) <= gen.AGENTS_MAX_COUNT


def test_units_cross_the_seam_exactly_once():
    """Extents stay in Source units; only the agents are centimetres."""

    document, _step, rows = _rows()
    human = next(row for row in rows if row["name"] == "Human")
    assert human["radiusCm"] == 13.0 * gen.UNITS_TO_CM == 33.02
    assert human["heightCm"] == 72.0 * gen.UNITS_TO_CM == 182.88
    assert human["stepCm"] == 18.0 * gen.UNITS_TO_CM == 45.72
    # The emitted header carries retail's own numbers, unconverted.
    header = HEADER.read_text(encoding="utf-8")
    assert "FVector(-13.0, -13.0, 0.0), FVector(13.0, 13.0, 72.0)" in header
    assert "StepHeightUnits = 18.0f" in header
    assert "GraphBuildStepHeightUnits = 40.0f" in header
    assert "182.88" not in header, "centimetres leaked into the retail-units table"


def test_a_radius_is_half_the_greater_footprint():
    rows = {row["bit"]: row for row in gen.load(gen.HULL_TABLE)["rows"]}
    # WIDE_HUMAN is the asymmetric row: 35 wide in x, 30 in y.
    width, depth, height, radius = gen.dimensions(rows[3])
    assert (width, depth, height) == (35.0, 30.0, 72.0)
    assert radius == 17.5
    assert not gen.is_symmetric(rows[3])
    # Every hull that becomes an agent is symmetric, so its radius is unambiguous.
    for bit in gen.LINKED_HULLS:
        assert gen.is_symmetric(rows[bit]), rows[bit]["name"]


def test_the_cell_size_scales_with_the_agent():
    """One project-wide cell would lose the rat: its radius is smaller than the human's cell."""

    _document, _step, rows = _rows()
    by_name = {row["name"]: row for row in rows}
    assert by_name["Rat"]["cellSizeCm"] == 5.0
    assert by_name["Human"]["cellSizeCm"] == 10.0
    assert by_name["Werewolf"]["cellSizeCm"] == 15.0
    assert by_name["Rat"]["radiusCm"] > by_name["Rat"]["cellSizeCm"]
    for row in rows:
        assert row["radiusCm"] > row["cellSizeCm"], f"{row['name']} is thinner than its cell"


def test_agent_names_are_readable_and_unique():
    _document, _step, rows = _rows()
    names = [row["name"] for row in rows]
    assert len(set(names)) == len(names)
    assert "Human" in names and "Rat" in names and "MingXiaoPathing" in names
    assert all(name.isidentifier() for name in names)


def test_the_header_marks_which_hulls_carry_links():
    header = HEADER.read_text(encoding="utf-8")
    # bit 0 HUMAN carries links, bit 1 HUMAN_PATHING does not.
    assert 'TEXT("HUMAN_HULL")' in header and 'TEXT("HUMAN_PATHING_HULL")' in header
    for line in header.splitlines():
        if 'TEXT("HUMAN_PATHING_HULL")' in line:
            assert "false" in line
        if 'TEXT("RAT_HULL")' in line:
            assert "true" in line


def test_the_agents_are_not_installed_until_something_restricts_them():
    """14 declared agents with auto-create on would build 14 meshes on every map load.

    The block is generated and tested here; it reaches `DefaultEngine.ini` only when job 5 adds
    the per-map `SupportedAgentsMask` and turns `bAutoCreateNavigationData` off.
    """

    assert gen.EMIT_AGENTS_INI is False
    ini = (REPO / "Config" / "DefaultEngine.ini").read_text(encoding="utf-8")
    assert "+SupportedAgents=" not in ini
    assert gen.BEGIN_MARKER not in ini


def test_the_ini_block_is_ascii_and_round_trips():
    _document, _step, rows = _rows()
    block = gen.emit_ini_block(rows)
    assert block.isascii()
    assert block.count("+SupportedAgents=") == 14
    first = gen.splice_ini("[/Script/NavigationSystem.RecastNavMesh]\nRuntimeGeneration=Dynamic\n",
                           block)
    assert "[/Script/NavigationSystem.NavigationSystemV1]" in first
    assert gen.splice_ini(first, block) == first


def test_the_default_hull_is_human_while_the_stand_hull_is_unrecovered():
    assert gen.DEFAULT_AGENT_HULL == 0
    assert "DefaultHull = 0" in HEADER.read_text(encoding="utf-8")
