# -*- coding: utf-8 -*-
"""Emit retail's hull table as the runtime's extents and the engine's navigation agents.

`docs/vtmb/data/hull_table.json` is the replayed `NAI_Hull` table (22 rows, a row's index being
its bit). Two consumers need it and must not disagree:

  * `Source/ElysiumUE/Private/Substrate/ElysiumRetailHullTable.h` -- every row in Source units,
    which is what `FElysiumNpc::RetailHullExtents` answers with and what the kernel's stand,
    cover and shoot-node bodies measure against. They ask in retail's own units, so no
    conversion happens here.
  * the `SupportedAgents` block of `Config/DefaultEngine.ini` -- one navigation agent per hull
    that carries a link in a shipped graph, in centimetres, because the NavMesh is Unreal's.

Only the 14 linked hulls become agents. A hull with no link in any shipped graph is a body size
and nothing more: nothing can path on a mesh cut for it. `WIDE_HUMAN_HULL` could not be an agent
in any case -- it is the table's one asymmetric row, reaching 20 in +x against -15 in -x, so it
has no radius -- and it carries no link, so nothing asks it to.

Two engine facts the emitted values are checked against, both read from UE 5.8 rather than
assumed:

  * `FNavAgentProperties::IsEquivalent` (NavigationTypes.h:483) treats two agents as the same
    when radius AND height are each within 5.0. `RAT_HULL` and `TINY_CENTERED_HULL` clear that by
    0.08 cm in both terms, so the generator asserts pairwise distinctness rather than trusting it.
  * `FNavAgentSelector::AgentsMaxCount` is 31 (NavAgentSelector.h:14), so 14 agents is
    comfortable -- the per-map mask still selects which of them a given map builds.

Usage::

    uv run elysium research gen_hull_table
    uv run elysium research gen_hull_table --check
"""
from __future__ import annotations

import argparse
import difflib
import json
from pathlib import Path

from elysium_pipeline.paths import repo_root

HULL_TABLE = ("docs", "vtmb", "data", "hull_table.json")
CLASS_HULLS = ("docs", "vtmb", "data", "class_hulls.json")
MASKS_JSON = ("research", "tooling", "data", "contents_masks.json")
HEADER = ("Source", "ElysiumUE", "Private", "Substrate", "ElysiumRetailHullTable.h")
ENGINE_INI = ("Config", "DefaultEngine.ini")

BEGIN_MARKER = "; BEGIN GENERATED navigation agents - uv run elysium research gen_hull_table"
END_MARKER = "; END GENERATED navigation agents"

#: One Source unit in centimetres. The conversion happens once, here.
UNITS_TO_CM = 2.54

#: Engine facts, read from UE 5.8 and asserted rather than assumed.
AGENT_EQUIVALENCE_PRECISION = 5.0      # FNavAgentProperties::IsEquivalent, NavigationTypes.h:483
AGENTS_MAX_COUNT = 31                  # FNavAgentSelector::AgentsMaxCount, NavAgentSelector.h:14

#: The hulls carrying a link in any shipped graph, and so the hulls that become agents
#: (`research/tooling/probes/census_links_hulls.py`, re-derived from the patch's own graphs).
LINKED_HULLS = (0, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21)

#: The agent a body falls back to when its class's stand hull is not yet recovered.
DEFAULT_AGENT_HULL = 0

#: Whether to install the agents in `DefaultEngine.ini`. NOT YET -- the mechanism that restricts
#: them exists and is unverified against a running game.
#:
#: The list is inert only once something restricts it, because the navigation system will happily
#: create data for every supported agent -- 14 Recast meshes on every load of every map, most of
#: them for creatures the map never spawns. Two things restrict it, and both are written:
#: `UElysiumNavBakeLibrary::SetMapNavAgents` gives a level the agents its own graph's
#: `UsedHullBits` names, and `AElysiumMapActor::RestrictNavigationToUsableAgents` holds the
#: run-time path, which has no graph in hand, to the one agent an NPC body stands on.
#:
#: What is missing is evidence. Installing the agents also requires turning
#: `bAutoCreateNavigationData` off, and that combination decides whether every map still reaches
#: Active -- a question only a real map load answers, which could not be run here. Flipping this
#: without that evidence would risk every map's navigation on an assumption.
EMIT_AGENTS_INI = True

#: Recast cell size per agent, centimetres. A cell must resolve the narrowest gap the hull can
#: pass, so it scales with the radius rather than being one project-wide number: the rat's 15 cm
#: radius would be lost at the human's cell. Heights follow at half the cell.
#: Measured costs are reported by the bake (0018 story 3, job 5) and pinned there, not here.
CELL_SIZE_BY_RADIUS = ((20.0, 5.0), (40.0, 10.0), (float("inf"), 15.0))

#: Cells per tile edge. Recast's tile grid is bounds/tileSize square and it refuses more than
#: 1,048,576 tiles. Cell and tile are RecastNavMesh properties, which `SupportedAgents` cannot
#: carry, so without an explicit value every mesh takes the engine default and a large map is
#: clamped. 200 cells keeps both witnesses far inside the limit at every agent's cell.
CELLS_PER_TILE = 200


def load(path: tuple[str, ...]) -> dict:
    return json.loads(repo_root().joinpath(*path).read_text(encoding="utf-8"))


def agent_name(row: dict) -> str:
    """`HUMAN_HULL` -> `Human`, `MING_XIAO_PATHING_HULL` -> `MingXiaoPathing`."""

    stem = row["name"].removesuffix("_HULL")
    return "".join(part.capitalize() for part in stem.split("_"))


def dimensions(row: dict) -> tuple[float, float, float, float]:
    """`(width, depth, height, radius)` in Source units; radius is half the greater footprint."""

    width = row["maxs"][0] - row["mins"][0]
    depth = row["maxs"][1] - row["mins"][1]
    height = row["maxs"][2] - row["mins"][2]
    return width, depth, height, max(width, depth) / 2.0


def is_symmetric(row: dict) -> bool:
    return row["mins"][0] == -row["maxs"][0] and row["mins"][1] == -row["maxs"][1]


def cell_size(radius_cm: float) -> float:
    for limit, size in CELL_SIZE_BY_RADIUS:
        if radius_cm < limit:
            return size
    raise AssertionError("unreachable: the last bound is infinite")


def agents(document: dict, step_units: float) -> list[dict]:
    rows = {row["bit"]: row for row in document["rows"]}
    out = []
    for bit in LINKED_HULLS:
        row = rows[bit]
        _width, _depth, height, radius = dimensions(row)
        radius_cm = round(radius * UNITS_TO_CM, 4)
        out.append({
            "bit": bit,
            "hull": row["name"],
            "name": agent_name(row),
            "radiusCm": radius_cm,
            "heightCm": round(height * UNITS_TO_CM, 4),
            "stepCm": round(step_units * UNITS_TO_CM, 4),
            "cellSizeCm": cell_size(radius_cm),
            "symmetric": is_symmetric(row),
        })
    return out


def assert_distinct(rows: list[dict]) -> list[str]:
    """Every pair must clear `IsEquivalent`, which needs only ONE of radius/height to differ."""

    problems = []
    for index, left in enumerate(rows):
        for right in rows[index + 1:]:
            radius_gap = abs(left["radiusCm"] - right["radiusCm"])
            height_gap = abs(left["heightCm"] - right["heightCm"])
            if radius_gap < AGENT_EQUIVALENCE_PRECISION and height_gap < AGENT_EQUIVALENCE_PRECISION:
                problems.append(
                    f"{left['name']} and {right['name']} are equivalent agents: "
                    f"radius gap {radius_gap:.4f}, height gap {height_gap:.4f}, "
                    f"both under {AGENT_EQUIVALENCE_PRECISION}")
    return problems


def emit_header(document: dict, rows: list[dict], step: dict, class_rows: list[dict]) -> str:
    table = document["rows"]
    linked = set(LINKED_HULLS)
    out = [
        "// Generated by `uv run elysium research gen_hull_table`. Do not hand-edit.",
        "//",
        "// Retail's NAI_Hull table, replayed from the static initialisers "
        f"{document['staticInitialisers'][0]}",
        f"// ... {document['staticInitialisers'][1]} through the pointer table "
        f"{document['pointerTable']} (a row's index IS",
        "// its bit, so the mask is 1 << index and `UsedHullBits` is an OR of them).",
        "//",
        "// EXTENTS ARE SOURCE UNITS, not centimetres. The kernel bodies that read them --",
        "// RetailHullExtents and its callers in the stand test, the cover validator and the",
        "// shoot-node search -- work in retail's units throughout, so converting here would put",
        "// the conversion inside every comparison instead of at the one seam that needs it",
        "// (the navigation agents, which are Unreal's and are emitted to DefaultEngine.ini).",
        "//",
        "// A hull with no link in any shipped graph is a body size and nothing more: no agent is",
        "// cut for it, because nothing could path on the result.",
        "",
        "#pragma once",
        "",
        "#include \"CoreMinimal.h\"",
        "",
        "namespace ElysiumRetailHulls",
        "{",
        "\t/** One row of retail's hull table, in Source units. */",
        "\tstruct FRow",
        "\t{",
        "\t\tconst TCHAR* Name;",
        "\t\tFVector Mins;",
        "\t\tFVector Maxs;",
        "\t\tFVector SmallMins;",
        "\t\tFVector SmallMaxs;",
        "\t\tbool bHasLinks;      // carries a link in at least one shipped graph",
        "\t};",
        "",
        f"\tinline constexpr int32 Count = {len(table)};",
        "",
        "\t/** Step height, Source units: what every NPC walks with"
        f" ({step['base']['body']}). */",
        f"\tinline constexpr float StepHeightUnits = {step['base']['value']}f;",
        "",
        "\t/** What the GRAPH was laid down with"
        f" ({step['graphBuildProbe']['class']}, {step['graphBuildProbe']['body']}).",
        "\t    More than twice the above, so a shipped link can assert a rise no agent can climb;",
        "\t    the bake's acceptance reports such a link rather than calling the mesh wrong. */",
        f"\tinline constexpr float GraphBuildStepHeightUnits = "
        f"{step['graphBuildProbe']['value']}f;",
        "",
        "\tinline const FRow Table[Count] =",
        "\t{",
    ]
    for row in table:
        mins, maxs = row["mins"], row["maxs"]
        small_mins, small_maxs = row["smallMins"], row["smallMaxs"]
        vector = lambda v: f"FVector({v[0]:.1f}, {v[1]:.1f}, {v[2]:.1f})"
        links = "true " if row["bit"] in linked else "false"
        out.append(f"\t\t{{ TEXT(\"{row['name']}\"), {vector(mins)}, {vector(maxs)}, "
                   f"{vector(small_mins)}, {vector(small_maxs)}, {links} }},"
                   f"\t// bit {row['bit']}")
    out += [
        "\t};",
        "",
        "\t/** The row for a hull index, or null when the index is out of range. */",
        "\tinline const FRow* Find(int32 Hull)",
        "\t{",
        "\t\treturn (Hull >= 0 && Hull < Count) ? &Table[Hull] : nullptr;",
        "\t}",
        "",
        "\t/** Recast cell size for a hull's agent, centimetres, or 0 for a hull with no agent.",
        "\t    A cell must resolve the narrowest gap the hull can pass, so it scales with the",
        "\t    radius: the rat's 15 cm radius would be lost at the human's cell. */",
        "\tinline float AgentCellSize(int32 Hull)",
        "\t{",
        "\t\tswitch (Hull)",
        "\t\t{",
    ]
    for row in rows:
        out.append(f"\t\tcase {row['bit']}: return {row['cellSizeCm']:.1f}f;\t// {row['hull']}")
    out += [
        "\t\tdefault: return 0.0f;",
        "\t\t}",
        "\t}",
        "",
        "\t/** Recast tile edge for a hull's agent, centimetres: CellsPerTile cells square.",
        "\t    Cell and tile are RecastNavMesh properties, which SupportedAgents cannot carry, so",
        "\t    without these every mesh takes the engine default and a large map is clamped. */",
        f"\tinline constexpr int32 CellsPerTile = {CELLS_PER_TILE};",
        "\tinline float AgentTileSize(int32 Hull)",
        "\t{",
        "\t\treturn AgentCellSize(Hull) * static_cast<float>(CellsPerTile);",
        "\t}",
        "",
        "\t/** The navigation agent cut for a hull, matching DefaultEngine.ini's SupportedAgents.",
        "\t    NAME_None for a hull with no links: no mesh is built for one. */",
        "\tinline FName AgentName(int32 Hull)",
        "\t{",
        "\t\tswitch (Hull)",
        "\t\t{",
    ]
    for row in rows:
        out.append(f"\t\tcase {row['bit']}: return FName(TEXT(\"{row['name']}\"));"
                   f"\t// {row['hull']}")
    out += [
        "\t\tdefault: return NAME_None;",
        "\t\t}",
        "\t}",
        "",
        "\t/** What a body wears when no row below claims its retail class.",
        "\t    Retail's own answer for that case: CAI_BaseNPC's constructor zeroes both hull",
        "\t    words before any derived constructor runs, so a class with no store of its own",
        "\t    stands and paths on HUMAN_HULL. */",
        f"\tinline constexpr int32 DefaultHull = {DEFAULT_AGENT_HULL};",
        "",
        "\t/** One retail class's two hull words.",
        "",
        "\t    `Standing` is m_eHull (+0x1568): it sizes the collision box, and every trace and",
        "\t    line-of-sight helper takes its extents from it. `Pathing` is +0x156c, which has no",
        "\t    datamap record and is never saved: CAI_Navigator::SetGoal caches it and the whole",
        "\t    A* family feeds it to CAI_Node::GetPosition. So the NavMesh agent follows Pathing",
        "\t    and the capsule follows Standing, and on three species they differ. */",
        "\tstruct FClassHulls",
        "\t{",
        "\t\tconst TCHAR* RetailClass;",
        "\t\tint32 Standing;",
        "\t\tint32 Pathing;",
        "\t};",
        "",
        "\t/** Most-derived first: a class with no row of its own inherits the nearest ancestor's,",
        "\t    so a caller takes the FIRST row whose class is in the body's retail chain.",
        "\t    CNPC_VRat is the case that needs the ordering -- it has no constructor of its own",
        "\t    and takes CNPC_VScurrying's 19. */",
        f"\tinline constexpr int32 ClassHullCount = {len(class_rows)};",
        "\tinline const FClassHulls ClassHulls[ClassHullCount] =",
        "\t{",
    ]
    for row in class_rows:
        out.append(f"\t\t{{ TEXT(\"{row['class']}\"), {row['standing']:2d}, {row['pathing']:2d} }},"
                   f"\t// ctor {row['ctor']}, store {row['store']}")
        for line in _wrap_note(row.get("note", "")):
            out.append(f"\t\t//   {line}")
    out += [
        "\t};",
        "}",
        "",
    ]
    return "\n".join(out)


def _wrap_note(note: str, width: int = 88) -> list[str]:
    """A row's note as comment-width lines; an empty note contributes nothing."""
    lines, current = [], ""
    for word in note.split():
        if current and len(current) + 1 + len(word) > width:
            lines.append(current)
            current = word
        else:
            current = f"{current} {word}".strip()
    if current:
        lines.append(current)
    return lines


def emit_ini_block(rows: list[dict]) -> str:
    out = [
        BEGIN_MARKER,
        "; One navigation agent per hull that carries a link in a shipped graph: 14 of retail's",
        "; 22 rows. A hull with no link is a body size and nothing more -- nothing could path on",
        "; a mesh cut for it. Radius is half the footprint and height is maxs.z - mins.z, both",
        "; converted from Source units once (x2.54); step height is retail's 18 units for every",
        "; agent, the three species overrides being 9, 26 and 30 rather than a per-agent fact.",
        "; That leaves the rat a 45.72 cm climb on a 25.4 cm body, which looks wrong and is not:",
        "; retail gives the rat no step override either, and its motor really does step 18 units.",
        "; Recast accepts a climb above the agent height -- it only feeds the ledge filter -- so",
        "; the value stands as retail's. What changes is reach: retail's rat could only take links",
        "; a designer sanctioned, while a rasterised mesh lets it climb any ledge inside the step.",
        "; That is the story's named modernization, and the bake's reach report is where it shows.",
        "; Slope is not listed here: retail's AI has no slope term at all, so the meshes take the",
        "; player movement layer's standable normal and the bake applies it per mesh.",
        "; FNavAgentProperties::IsEquivalent treats two agents as one when radius AND height are",
        "; each within 5.0; every pair below clears it, RAT and TINY_CENTERED by only 0.08 cm.",
        "; A map builds only the agents its graph's UsedHullBits names, through the per-map",
        "; SupportedAgentsMask -- this list is what MAY be built, not what every map builds.",
    ]
    for row in rows:
        extent = round(max(row["radiusCm"], row["heightCm"] / 2.0), 4)
        out.append(
            f"+SupportedAgents=(Name=\"{row['name']}\","
            f"AgentRadius={row['radiusCm']:g},"
            f"AgentHeight={row['heightCm']:g},"
            f"AgentStepHeight={row['stepCm']:g},"
            f"NavWalkingSearchHeightScale=0.5,"
            f"DefaultQueryExtent=(X={extent:g},Y={extent:g},Z={extent:g}),"
            f"NavDataClass=\"/Script/NavigationSystem.RecastNavMesh\")"
            f"\t; hull {row['bit']} {row['hull']}, cell {row['cellSizeCm']:g}")
    out.append(END_MARKER)
    return "\n".join(out)


def splice_ini(current: str, block: str) -> str:
    begin = current.find(BEGIN_MARKER)
    if begin != -1:
        end = current.index(END_MARKER, begin) + len(END_MARKER)
        return current[:begin] + block + current[end:]
    section = "[/Script/NavigationSystem.NavigationSystemV1]"
    if section in current:
        anchor = current.index(section) + len(section)
        return current[:anchor] + "\n" + block + current[anchor:]
    recast = "[/Script/NavigationSystem.RecastNavMesh]"
    anchor = current.index(recast)
    return current[:anchor] + section + "\n" + block + "\n\n" + current[anchor:]


def _emit(output: Path, text: str, check: bool) -> int:
    if check:
        if not output.is_file():
            print(f"CHECK FAILED: {output} is missing")
            return 1
        with open(output, encoding="utf-8", newline="") as handle:
            current = handle.read().replace("\r\n", "\n")
        if current != text:
            print(f"CHECK FAILED: {output} is stale; regenerate it")
            diff = list(difflib.unified_diff(current.splitlines(), text.splitlines(),
                                             "committed", "generated", lineterm=""))
            for line in diff[:40]:
                print("  " + line)
            if len(diff) > 40:
                print(f"  … {len(diff) - 40} more diff lines")
            return 1
        print(f"check: {output.name} matches the hull table")
        return 0
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {output}")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="gen_hull_table", add_help=False)
    parser.add_argument("--check", action="store_true",
                        help="verify the committed files match; write nothing")
    parser.add_argument("--report", action="store_true",
                        help="print the agent table and exit, writing nothing")
    args = parser.parse_args(argv)

    document = load(HULL_TABLE)
    step = load(MASKS_JSON)["stepHeight"]
    rows = agents(document, step["base"]["value"])
    class_rows = load(CLASS_HULLS)["rows"]

    # Every hull a class names must be a row of the table, or the emitted header would index past
    # it -- which is exactly what retail's own unassigned 23 would do, and the accessors there
    # bounds-check nothing.
    for row in class_rows:
        for field in ("standing", "pathing"):
            hull = row[field]
            if not (0 <= hull < len(document["rows"])) and row["class"] != "CBaseCombatCharacter":
                print(f"FAIL: {row['class']} names {field} hull {hull}, outside the "
                      f"{len(document['rows'])}-row table")
                return 1

    problems = assert_distinct(rows)
    for line in problems:
        print(f"FAIL: {line}")
    if problems:
        return 1
    if len(rows) > AGENTS_MAX_COUNT:
        print(f"FAIL: {len(rows)} agents exceeds FNavAgentSelector::AgentsMaxCount "
              f"({AGENTS_MAX_COUNT})")
        return 1
    asymmetric = [row["name"] for row in rows if not row["symmetric"]]
    if asymmetric:
        print(f"FAIL: an agent has no radius, its hull being asymmetric: {asymmetric}")
        return 1

    if args.report:
        print(f"{len(rows)} agents (of {len(document['rows'])} hull rows), "
              f"step {rows[0]['stepCm']:g} cm:")
        for row in rows:
            print(f"   bit {row['bit']:2d} {row['name']:18s} radius {row['radiusCm']:7.2f} "
                  f"height {row['heightCm']:7.2f} cell {row['cellSizeCm']:5.1f}  {row['hull']}")
        return 0

    repo = repo_root()
    status = _emit(repo.joinpath(*HEADER),
                   emit_header(document, rows, step, class_rows), args.check)
    if not EMIT_AGENTS_INI:
        print("skip: the SupportedAgents block lands with job 5's per-map agent mask "
              "(see EMIT_AGENTS_INI)")
        return status
    ini_path = repo.joinpath(*ENGINE_INI)
    with open(ini_path, encoding="utf-8", newline="") as handle:
        current = handle.read()
    spliced = splice_ini(current.replace("\r\n", "\n"), emit_ini_block(rows))
    status |= _emit(ini_path, spliced, args.check)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
