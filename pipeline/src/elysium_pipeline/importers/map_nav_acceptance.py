"""Retail's graph as an answer key for the baked meshes.

Every check the bake's navigation arm runs needs the same thing first: retail's own statement of
where an NPC of a given hull could walk, projected into the world the port baked. That is what this
stage produces. It routes nothing -- Unreal does the routing -- it only says what the answer should
be, so a mesh can be judged against a record rather than looked at.

Three things it states, each a different question about the same graph:

  * **Ground links, per agent.** A link carrying ground motion for a hull is retail asserting that
    a body of that hull gets from one node to the other. The agent's mesh has to agree.
  * **The links only one agent has.** The rat hull is not a subset of the human one: 41 of the
    tutorial's 428 rat links and 99 of the hub's 1,862 carry no human motion. A pair of meshes that
    is really one mesh passes every human check and fails these, which is what makes them worth
    stating separately.
  * **The bridging links.** The subset of those that join node sets the HUMAN links keep apart --
    the hub's nine. They are the sharpest form of the same question: if the rat's mesh were the
    human's, these two halves of the map could not be joined at all.

And one thing it predicts rather than checks: retail's graph was laid down by `CAI_TestHull`, which
steps **40** units (`0x102d72b0`), while every NPC steps **18** (`CAI_BaseNPC::StepHeight`
`0x101a6b40`; only Ming Xiao 30, its tentacle 9 and Tzimisce 26 differ). A link asserting a rise
between the two is unreachable for a real agent and is reported with its rise measured, not counted
a mesh defect. Cutting the agents at 40 instead would let NPCs climb what retail's own motor
refuses.

And one thing it expects of the bake rather than of a mesh (`bakedJumpLinks`, 0018/7): every
human jump-only pair recorded as one `AElysiumNavJumpLink`, present and DISABLED with no agent.
Retail plans no jump link -- `0x102ff960` step 2 ANDs the NPC's capabilities (slot 513) with the
link's per-hull word and no NPC holds bit 2 -- so an enabled one is a route retail does not have,
and fails the gate. The graph's jump rows (`perHull[h]["jump"]`) stay: they describe the graph.

Beside the checks the key carries the place reports' questions (`placeQueries`, 0018 story 4):
which places and authored points sit off each agent's mesh, which authored points no node covers,
and whether each agent's mesh joins what the graph's zones keep apart. Those are observations --
they are reported and never fail the gate; their pins are 0018/21-9's.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Any, Iterable, Sequence

from elysium_pipeline.formats.bsp import source_to_unreal
from elysium_pipeline.formats.nav_graph_glb.model import LINK_OFF, NODE_GROUND

RETAIL_HULL_COUNT = 22

#: `0x102ff960` reads a link's per-hull motion word; bit 0 is ground and bit 1 is jump.
MOVE_GROUND = 1
MOVE_JUMP = 2

#: One Source unit in centimetres.
UNITS_TO_CM = 2.54


class NavAcceptanceError(ValueError):
    """The graph cannot be projected into an answer key."""


def _node_position_units(node: dict, hull: int) -> list[float]:
    origin = [float(value) for value in node["origin"]["source"]]
    offsets = node.get("hullOffsets") or []
    if len(origin) != 3 or len(offsets) != RETAIL_HULL_COUNT:
        raise NavAcceptanceError("AIN node has an invalid position/hull-offset table")
    if node.get("type") == NODE_GROUND:
        origin[2] += float(offsets[hull])
    if not all(math.isfinite(value) for value in origin):
        raise NavAcceptanceError("AIN node position is not finite")
    return origin


def _endpoint_cm(node: dict, hull: int) -> list[float]:
    return list(source_to_unreal(*_node_position_units(node, hull)))


class _Components:
    """Union-find over node ids, for "do the human's links already join these two"."""

    def __init__(self) -> None:
        self._parent: dict[int, int] = {}

    def find(self, node: int) -> int:
        self._parent.setdefault(node, node)
        while self._parent[node] != node:
            self._parent[node] = self._parent[self._parent[node]]
            node = self._parent[node]
        return node

    def union(self, a: int, b: int) -> None:
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self._parent[ra] = rb

    def joined(self, a: int, b: int) -> bool:
        return self.find(a) == self.find(b)


def _usable_links(block: dict) -> list[dict]:
    links = []
    for link in block["links"]:
        fields = link["fields"]
        if len(fields) != RETAIL_HULL_COUNT + 1:
            raise NavAcceptanceError(f"AIN link {link.get('index')}: incomplete motion fields")
        if int(fields[0]) & LINK_OFF:
            continue
        links.append(link)
    return links


def _motion(link: dict, hull: int) -> int:
    return int(link["fields"][1 + hull])


def project(block: dict, map_name: str, *, base_hull: int = 0,
            step_height_units: float = 18.0,
            graph_step_height_units: float = 40.0) -> dict[str, Any]:
    """Retail's answer key for one map: per-agent links, the agent-only set, and the bridges."""

    header = block["header"]
    if int(header["version"]) != 30 or int(header["numHulls"]) != RETAIL_HULL_COUNT:
        raise NavAcceptanceError("nav acceptance requires retail version 30 and 22 hulls")
    nodes = {int(node["index"]): node for node in block["nodes"]}
    if len(nodes) != len(block["nodes"]):
        raise NavAcceptanceError("AIN node ids are not unique")

    used_bits = int(header["usedHullBits"]["value"])
    hulls = [bit for bit in range(RETAIL_HULL_COUNT) if used_bits & (1 << bit)]
    if base_hull not in hulls:
        raise NavAcceptanceError(
            f"{map_name}: hull {base_hull} is not in UsedHullBits {used_bits:#x}")
    links = _usable_links(block)

    # What the base hull -- the human -- already joins. Everything below is measured against it,
    # because "only the rat has this link" is only interesting where the human has no other way.
    #
    # 0018/7, two decisions RECORDED FOR THE CLOSE (reported, never failed):
    #  (a) Bridging keeps the GRAPH's ground | jump union below: this key describes the graph.
    #      Under retail reach -- no NPC takes a jump-only link, `0x102ff960` step 2 -- the hub's
    #      rat-only links 727, 1242, 1243 and 1244 join nodes the human joins only by a jump and
    #      would be bridging too (9 -> 13; the tutorial's 5 do not move).
    #      REVERSED for the VERDICT (V13, N20): the key still lists every bridging row, union
    #      included, but `nav_acceptance.bridging_errors` now fails only the rows that carry
    #      GROUND for this hull and reports the jump-only ones -- retail's rat never plans one
    #      (`0x102ff960` step 2 ANDs slot 513 with the link's per-hull word; no NPC holds bit 2),
    #      so demanding the rat's mesh walk it asks for reach retail does not have. The tutorial's
    #      rat link 105 (44 -> 69, `fields[20]=2`) is the row that made it a finding.
    #  (b) Only the HUMAN jump-only pairs get a record actor (`map_jump_links`, 25 tutorial / 117
    #      hub); the rat-only jump-only links (tutorial 19, hub 35) are counted in the staged
    #      summary and not recorded.
    base = _Components()
    for link in links:
        if _motion(link, base_hull) & (MOVE_GROUND | MOVE_JUMP):
            base.union(int(link["src"]), int(link["dst"]))

    per_hull: dict[str, Any] = {}
    outliers: list[dict[str, Any]] = []
    for hull in hulls:
        ground, jump = [], []
        for link in links:
            motion = _motion(link, hull)
            if not motion:
                continue
            src, dst = int(link["src"]), int(link["dst"])
            if src not in nodes or dst not in nodes:
                raise NavAcceptanceError(f"AIN link {link['index']}: unknown endpoint")
            start_units = _node_position_units(nodes[src], hull)
            end_units = _node_position_units(nodes[dst], hull)
            row = {
                "index": int(link["index"]),
                "src": src,
                "dst": dst,
                "startCm": list(source_to_unreal(*start_units)),
                "endCm": list(source_to_unreal(*end_units)),
                "straightCm": math.dist(source_to_unreal(*start_units),
                                        source_to_unreal(*end_units)),
            }
            if motion & MOVE_GROUND:
                rise = abs(end_units[2] - start_units[2])
                row["riseUnits"] = rise
                ground.append(row)
                # Reported, not failed: retail's own graph asserts a walk its own NPCs cannot make.
                if step_height_units < rise <= graph_step_height_units:
                    outliers.append({
                        "index": row["index"], "hull": hull, "riseUnits": rise,
                        "reason": "rise is inside the graph builder's step and above the NPC's",
                    })
            if motion & MOVE_JUMP:
                # A graph row, never a route: the endpoints must land (a retail ground link
                # reaches each), the jump between them is refused at `0x102ff960` step 2. The
                # word rides along so the gate can tell a jump-only pair (no retail edge for this
                # hull at all -- `InitLinks 0x102fb4e0`'s walk failed there) and ask whether the
                # mesh walks it anyway.
                jump.append({**row, "motion": motion})
        per_hull[str(hull)] = {"ground": ground, "jump": jump}

    # The links this agent has and the base hull does not, and the subset of those that JOIN --
    # where the base hull has no other route between the two ends.
    agent_only: dict[str, Any] = {}
    for hull in hulls:
        if hull == base_hull:
            continue
        only, bridging = [], []
        for link in links:
            if not _motion(link, hull) or _motion(link, base_hull):
                continue
            src, dst = int(link["src"]), int(link["dst"])
            # Both endpoint sets, here rather than at the query: a bridging link has NO row in
            # the base hull's link table (that is what makes it bridging), so a caller looking its
            # positions up there finds nothing and compares a degenerate segment instead -- which
            # reads as "the base agent paths this in 0 cm" and indicts a mesh that is fine.
            # The two differ only by each hull's own Z offset at the same two nodes.
            # `motion` rides along so the verdict can tell a jump-only bridge (no NPC plans it,
            # `0x102ff960` step 2) from a ground one (`nav_acceptance.bridging_errors`).
            row = {
                "index": int(link["index"]), "src": src, "dst": dst,
                "motion": _motion(link, hull),
                "startCm": _endpoint_cm(nodes[src], hull),
                "endCm": _endpoint_cm(nodes[dst], hull),
                "baseStartCm": _endpoint_cm(nodes[src], base_hull),
                "baseEndCm": _endpoint_cm(nodes[dst], base_hull),
            }
            only.append(row)
            if not base.joined(src, dst):
                bridging.append(row)
        agent_only[str(hull)] = {"only": only, "bridging": bridging}

    return {
        "map": map_name,
        "usedHullBits": used_bits,
        "hulls": hulls,
        "baseHull": base_hull,
        "nodes": len(nodes),
        "links": len(links),
        "perHull": per_hull,
        "agentOnly": agent_only,
        "stepOutliers": outliers,
        "stepHeightUnits": step_height_units,
        "graphStepHeightUnits": graph_step_height_units,
    }


def summarise(key: dict[str, Any]) -> dict[str, Any]:
    """The counting half, for a report line and for the pins."""

    return {
        "map": key["map"],
        "nodes": key["nodes"],
        "links": key["links"],
        "hulls": key["hulls"],
        "groundByHull": {hull: len(rows["ground"]) for hull, rows in key["perHull"].items()},
        "jumpByHull": {hull: len(rows["jump"]) for hull, rows in key["perHull"].items()},
        "onlyByHull": {hull: len(rows["only"]) for hull, rows in key["agentOnly"].items()},
        "bridgingByHull": {hull: len(rows["bridging"])
                           for hull, rows in key["agentOnly"].items()},
        "stepOutliers": len(key["stepOutliers"]),
    }


def stage(map_name: str, root: Path | None = None) -> dict[str, Any]:
    """`manifest["navAcceptance"]` for one map, from its published nav-graph unit.

    The place reports' queries ride the same key (`placeQueries`). They are observations and must
    never stop the gate, so a map whose places cannot be staged carries the reason instead.
    """

    from elysium_pipeline.exporters import UE_map_sidecars as producer
    from elysium_pipeline.importers import map_ai_infra, map_places
    from elysium_pipeline.importers.map_nav_doors import load_graph_block

    block = load_graph_block(map_name, root)
    key = project(block, map_name)
    key["summary"] = summarise(key)
    key["bakedJumpLinks"] = baked_jump_links(block, map_name)
    try:
        units = producer.read_units(map_name, root)
        rows = units.entities["entities"]
        sky = producer.SkyScope(units, producer.entity_pair_blocks(rows))
        key["placeQueries"] = place_queries(
            block, map_places.stage_rows(map_name, rows, block),
            map_ai_infra.stage_rows(map_name, rows, sky), key["hulls"])
    except (ValueError, KeyError, OSError) as error:
        key["placeQueries"] = {"unavailable": f"{type(error).__name__}: {error}"}
    return key


def baked_jump_links(block: dict, map_name: str) -> dict[str, Any]:
    """What the bake must have left in the level: one disabled, agentless record per staged row.

    Stated from the SAME staging the bake authors from (`map_jump_links.project_graph`), so the key
    and the level cannot disagree about which pairs exist.
    """

    from elysium_pipeline.importers import map_jump_links

    staged = map_jump_links.project_graph(block, map_name)
    return {
        "expected": [int(row["index"]) for row in staged["links"]],
        "expectation": "present, disabled, no agent",
        "summary": staged["summary"],
    }


# --- The place reports (0018 story 4; observations only, pins are 0018/21-9's) -----------------

#: Isolated nodes carry zone 1 and flood fills start at 4 (`0x102f49c0`); `IsConnected 0x102f48b0`
#: answers 0 for a zone-1 node against anything, so zone 1 is a group of singletons, not a zone.
ZONE_ISOLATED = 1

#: `FUN_102f41b0`, the point search that binds a goal POSITION to a node on the navigator's node
#: arm: an axis-aligned box of half extent `_DAT_1046bacc` = 2048 units on all three axes around
#: the point, inclusive, over raw node origins; the ten nearest, the first whose ray `0x102f39a0`
#: from the point clears. The ray is not reproduced offline, so "a node in the box" is the
#: necessary half of retail's cover test and an uncovered count here is a lower bound.
GOAL_NODE_BOX_UNITS = 2048.0

#: How many node pairs stand for one zone pair. The nearest pair alone is one projection away from
#: an unanswered question, so the next nearest pairs sharing no node with it ride beside it; the
#: verdict takes the nearest that answers.
ZONE_PAIR_CANDIDATES = 3

#: Wider projection extents (cm, half extents X, Y, Z) asked after the gate's own
#: (`verify_nav.PROJECT_EXTENT`, 60/60/250), so a point off the mesh is reported with how far off
#: it is -- bracketed, because the verify library answers "lands" and not a distance.
WIDER_PROJECT_EXTENTS_CM: tuple[tuple[float, float, float], ...] = (
    (150.0, 150.0, 400.0), (400.0, 400.0, 800.0), (1000.0, 1000.0, 1500.0))


def _raw_cm(node: dict) -> list[float]:
    return list(source_to_unreal(*[float(value) for value in node["origin"]["source"]]))


def _components(links: Sequence[dict], hull: int, motion: int) -> _Components:
    joined = _Components()
    for link in links:
        if _motion(link, hull) & motion:
            joined.union(int(link["src"]), int(link["dst"]))
    return joined


def _zone_groups(nodes: Sequence[dict]) -> dict[int, list[int]]:
    """Zone id to its node indices, zone 1 (the isolated nodes) as one group, ascending."""
    groups: dict[int, list[int]] = {}
    for node in nodes:
        groups.setdefault(int(node["zone"]), []).append(int(node["index"]))
    return dict(sorted(groups.items()))


def _candidate_pairs(a: Sequence[int], b: Sequence[int], raw: dict[int, list[float]],
                     count: int = ZONE_PAIR_CANDIDATES) -> list[tuple[int, int, float]]:
    """The nearest node pairs across two groups by raw origin, nearest first: pairs sharing no
    node with an earlier pick first, then the nearest remaining ones if the groups are too small."""
    ranked = sorted(((math.dist(raw[i], raw[j]), i, j) for i in a for j in b))
    chosen: list[tuple[float, int, int]] = []
    used: set[int] = set()
    for row in ranked:
        if len(chosen) == count:
            break
        if row[1] not in used and row[2] not in used:
            chosen.append(row)
            used.update(row[1:])
    for row in ranked:
        if len(chosen) == count:
            break
        if row not in chosen:
            chosen.append(row)
    chosen.sort()
    return [(i, j, distance) for distance, i, j in chosen]


def _segment(nodes: dict[int, dict], src: int, dst: int, hull: int) -> dict[str, Any]:
    """Two nodes at `hull`'s own positions, as a path query row."""
    start, end = _endpoint_cm(nodes[src], hull), _endpoint_cm(nodes[dst], hull)
    return {"src": src, "dst": dst, "startCm": start, "endCm": end,
            "straightCm": math.dist(start, end)}


def _anchor(members: Sequence[int], nodes: dict[int, dict]) -> int:
    """The zone's best-connected node (most links, lowest index on a tie)."""
    return min(members, key=lambda index: (-int(nodes[index].get("linkCount") or 0), index))


def place_queries(block: dict, places: dict, infra: dict,
                  hulls: Sequence[int]) -> dict[str, Any]:
    """The three place reports' questions for one map, and the answer that needs no editor.

    * **Points** -- every node at its hull position (`CAI_Node::GetPosition 0x102fb0d0`: origin
      plus `zoffset[hull]` for a ground node, the raw origin for every other type; the climb arm's
      yaw shift is not mirrored and reads raw), then every staged hint, patrol point and
      interesting place at its own origin. Each is asked to project onto each agent's mesh.
    * **Coverage** -- answered here. A hint is covered iff `CNodeEnt::Spawn`'s pairing bound it to
      a node. An unpaired hint and an interesting place are covered iff a node's raw origin lies
      in `FUN_102f41b0`'s +-2048 box around it: `TASK_GET_PATH_TO_INTERESTING_PLACE` submits
      `m_vecInterestingPlace` (a spot `PickSpotFor 0x102da0d0` samples inside the place's bounds
      -- its origin stands for it here) as a type-8 goal with flags -1, whose node arm binds the
      goal end through that search; an unpaired hint's `GetPosition 0x102d1180` is its own origin,
      bound the same way.
    * **Zones** -- per zone pair, the nearest node pairs across them, asked to PATH; and per zone,
      its anchor to every other member, asked to path, for the converse.
    """

    nodes = {int(node["index"]): node for node in block["nodes"]}
    raw = {index: _raw_cm(node) for index, node in nodes.items()}
    links = _usable_links(block)
    paired = {int(place["hint"]) for place in places["places"] if int(place["hint"]) >= 0}

    points: list[dict[str, Any]] = [
        {"kind": "node", "id": index, "zone": int(nodes[index]["zone"]),
         "type": int(nodes[index]["type"])}
        for index in sorted(nodes)]
    authored: list[dict[str, Any]] = []
    for row in infra["rows"]:
        if row["family"] == "hint":
            kind = "patrol" if row["classname"].lower() == "info_node_patrol_point" else "hint"
        elif row["family"] == "place":
            kind = "place"
        else:
            continue
        authored.append({"kind": kind, "id": int(row["index"]), "name": row["targetname"],
                         "classname": row["classname"],
                         "originCm": [float(value) for value in row["originCm"]]})
    points.extend({k: v for k, v in row.items() if k != "originCm"} for row in authored)

    box_cm = GOAL_NODE_BOX_UNITS * UNITS_TO_CM
    uncovered = []
    farthest: dict[str, Any] | None = None
    for row in authored:
        if row["kind"] != "place" and row["id"] in paired:
            continue
        origin = row["originCm"]
        if raw:
            # The margin: how far the worst-served point stands from its nearest node, so a
            # clean count can be read against the box it passed.
            near = min(raw, key=lambda index: math.dist(raw[index], origin))
            gap = math.dist(raw[near], origin)
            if farthest is None or gap > farthest["nearestNodeCm"]:
                farthest = {"kind": row["kind"], "id": row["id"], "name": row["name"],
                            "nearestNode": near, "nearestNodeCm": round(gap, 1)}
        in_box = [index for index, position in raw.items()
                  if all(abs(position[axis] - origin[axis]) <= box_cm for axis in range(3))]
        if in_box:
            continue
        nearest = min(raw, key=lambda index: math.dist(raw[index], origin)) if raw else -1
        uncovered.append({
            "kind": row["kind"], "id": row["id"], "name": row["name"],
            "classname": row["classname"],
            "why": "no node in the +-2048 box" + ("" if row["kind"] == "place"
                                                   else " and not paired to a node"),
            "nearestNode": nearest,
            "nearestNodeCm": round(math.dist(raw[nearest], origin), 1) if raw else None,
        })
    coverage = {
        "boxUnits": GOAL_NODE_BOX_UNITS,
        "pairedHints": sum(1 for row in authored if row["kind"] != "place" and row["id"] in paired),
        "unpairedHints": sum(1 for row in authored
                             if row["kind"] != "place" and row["id"] not in paired),
        "places": sum(1 for row in authored if row["kind"] == "place"),
        "uncovered": uncovered,
        "farthestFromANode": farthest,
    }

    groups = _zone_groups(block["nodes"])
    zone_ids = list(groups)
    pairs = [(a, b, _candidate_pairs(groups[a], groups[b], raw))
             for position, a in enumerate(zone_ids) for b in zone_ids[position + 1:]]
    anchors = {zone: _anchor(members, nodes) for zone, members in groups.items()
               if zone != ZONE_ISOLATED and len(members) > 1}

    per_hull: dict[str, Any] = {}
    for hull in hulls:
        ground = _components(links, hull, MOVE_GROUND)
        linked = _components(links, hull, MOVE_GROUND | MOVE_JUMP)
        per_hull[str(hull)] = {
            "pointsCm": [_endpoint_cm(nodes[index], hull) for index in sorted(nodes)]
                        + [row["originCm"] for row in authored],
            "zonePairs": [
                {"zoneA": a, "zoneB": b,
                 "candidates": [_segment(nodes, i, j, hull) for i, j, _ in candidates]}
                for a, b, candidates in pairs],
            "sameZone": [
                {"zone": zone, "anchor": anchor, "node": member,
                 **_segment(nodes, anchor, member, hull),
                 "groundLinked": ground.joined(anchor, member),
                 "hullLinked": linked.joined(anchor, member)}
                for zone, anchor in anchors.items()
                for member in groups[zone] if member != anchor],
        }

    return {
        "points": points,
        "coverage": coverage,
        "zones": {str(zone): len(members) for zone, members in groups.items()},
        "widerExtentsCm": [list(extent) for extent in WIDER_PROJECT_EXTENTS_CM],
        "perHull": per_hull,
    }


def agent_name(hull_name: str) -> str:
    """`HUMAN_HULL` -> `Human`, the same rule `gen_hull_table` emits the ini block with.

    Stated here rather than imported because the generator is a research tool and this is the
    pipeline; if the two ever disagree, `test_nav_acceptance.py` fails on the committed
    `SupportedAgents` block rather than a mesh going quietly unfound.
    """

    return "".join(part.capitalize() for part in hull_name.removesuffix("_HULL").split("_"))


def agent_names(hulls: Iterable[int]) -> dict[str, str]:
    """Hull index (as a string key) to the navigation agent cut for it."""

    import json

    from elysium_pipeline.paths import repo_root

    rows = json.loads((repo_root() / "docs" / "vtmb" / "data" / "hull_table.json")
                      .read_text(encoding="utf-8"))["rows"]
    return {str(hull): agent_name(rows[hull]["name"]) for hull in hulls}
