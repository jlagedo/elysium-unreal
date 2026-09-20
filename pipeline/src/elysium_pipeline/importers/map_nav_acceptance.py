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
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Any, Iterable, Sequence

from elysium_pipeline.formats.bsp import source_to_unreal

RETAIL_HULL_COUNT = 22
LINK_OFF = 0x1000
NODE_GROUND = 2

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
    tail = node.get("tail") or []
    if tail and int(tail[0]) == NODE_GROUND:
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
                jump.append(row)
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
            row = {
                "index": int(link["index"]), "src": src, "dst": dst,
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
    """`manifest["navAcceptance"]` for one map, from its published nav-graph unit."""

    from elysium_pipeline.importers.map_nav_doors import load_graph_block

    key = project(load_graph_block(map_name, root), map_name)
    key["summary"] = summarise(key)
    return key


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
