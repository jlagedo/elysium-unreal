"""Stage one map's place set: retail's AIN nodes, the hints bound to them, and the wander caps
(0018 story 4).

A place is one node of the graph retail loads (`maps/graphs/<map>.ain`, the published nav-graph
unit), in network order: its type, flags, raw origin, yaw and all 22 hull Z offsets -- retail's own
row, because `CAI_Node::GetPosition 0x102fb0d0` can be asked for any hull -- plus the Hammer id the
graph compiled it from (`wcLookup`, provenance only) and the BSP entity index of the hint bound
to it.

**The binding is positional**, `CNodeEnt::Spawn 0x102d78d0` on the loaded branch, and nothing
else: node-classname rows are walked in BSP spawn order against a running counter
(`DAT_10926a3c`).

* `info_hint`, `info_node_kick_over`, `info_node_kick_at` and `info_node_shoot_at` never advance
  it. They make a hint with node id -1 iff their (class-forced) hint type is non-zero, else print
  `WARNING: Hint node with no hint type` and make nothing.
* `info_node_tzimisce` is renamed `info_node` first.
* Every other node-classname row advances it exactly once, whether or not it makes a hint (it
  does iff its hint type is non-zero or its `Group` is set). A hint is attached to
  `node[counter]` (`node+0xa0`) iff `0 <= counter < NumNodes`; otherwise `DAT_106c994c++` and the
  hint keeps its out-of-range id.

So the authored `nodeid` does not decide the pairing: a node's `wcId` and its hint row's `nodeid`
agree only when the AIN was built from this BSP (`navigation-jump-links.md` § "Which graph the
patched install runs on"). `pairing_report` counts where they diverge; that is retail behaviour.

The classname tables and the hint decision are `map_ai_infra`'s, which the hint actors are staged
by, so a row this lane binds is a row that lane bakes. A parented node row is refused the way
`map_ai_infra.stage_rows` refuses a parented hint: it would spawn out of BSP order and move the
counter.

Two further products ride the block. **Crosswalk pairs**: the links whose two endpoints are both
bound to a hint of type 11000 (`info_node_crosswalk`), for story 7's smart link. **Wander caps**:
per declared hull, 20 x the median raw-origin link length over the links that hull may use --
the reach of `TASK_GET_PATH_TO_RANDOM_NODE`'s walk at its `0x14` iteration guard
(`research/tooling/probes/census_link_lengths.py`). Source units, the unit schedule operands are
written in.
"""
from __future__ import annotations

import math
import statistics
from pathlib import Path
from typing import Any, Sequence

from elysium_pipeline.exporters import UE_map_sidecars as producer
from elysium_pipeline.formats.bsp import INCH_TO_CM, source_to_unreal
from elysium_pipeline.formats.nav_graph_glb.model import normalize_key
from elysium_pipeline.importers.map_ai_infra import (
    NODE_CLASSNAMES,
    STANDALONE_HINT_CLASSNAMES,
    _folded_get,
    _short,
    atoi,
    class_hint_type,
    makes_hint,
)

#: 1: the first block (0018 story 4). The C++ reader `UElysiumMapPlaces::AuthorJson` checks it.
VERSION = 1

#: Retail's hull count; a node row carries one Z offset per hull, a link one motion word.
RETAIL_HULL_COUNT = 22

#: The hull a declared hull with no link of its own borrows its wander cap from.
HUMAN_HULL = 0

#: `TASK_GET_PATH_TO_RANDOM_NODE`'s walk guard `0x14`: the cap is this many median hops.
WANDER_GUARD_HOPS = 0x14

#: `FUN_102d7d30`'s forced type for `info_node_crosswalk`.
CROSSWALK_HINT_TYPE = 11000


class MapPlacesError(ValueError):
    """A map's place set cannot be staged faithfully."""


def _spawn_classname(classname: str) -> str:
    """The classname `CNodeEnt::Spawn` compares, after its `info_node_tzimisce` rename."""
    name = classname.lower()
    return "info_node" if name == "info_node_tzimisce" else name


def _pair_hints(map_name: str, entity_rows: Sequence[dict[str, Any]],
                num_nodes: int) -> tuple[dict[int, tuple[int, int]], dict[str, Any]]:
    """`CNodeEnt::Spawn`'s counter over the node-classname rows, in BSP spawn order.

    Returns `{node: (bsp index, hint type)}` for every attached hint, and the `pairing` block.
    """
    pair_blocks = producer.entity_pair_blocks(entity_rows)
    attached: dict[int, tuple[int, int]] = {}
    out_of_range: list[dict[str, int]] = []
    standalone: list[int] = []
    counter = 0
    for index, (entity, pairs) in enumerate(zip(entity_rows, pair_blocks)):
        if int(entity.get("index", index)) != index:
            raise MapPlacesError(f"{map_name}: entity row {index} carries index {entity.get('index')}")
        classname = str(entity.get("classname") or "")
        if classname.lower() not in NODE_CLASSNAMES:
            continue
        _, keys = producer.collect_entity_fields(entity)
        if keys.get("parentname"):
            raise MapPlacesError(f"{map_name} entity {index} ({classname}): a parented node row "
                                 "spawns out of BSP order")
        name = _spawn_classname(classname)
        hint = makes_hint(classname, pairs)
        if name in STANDALONE_HINT_CLASSNAMES:
            # Node id -1, the counter untouched; a type-0 row makes nothing at all.
            if hint:
                standalone.append(index)
            continue
        if hint:
            if 0 <= counter < num_nodes:
                hint_type = _short(class_hint_type(name, atoi(_folded_get(pairs, "hinttype") or "")))
                attached[counter] = (index, hint_type)
            else:
                out_of_range.append({"bspIndex": index, "counter": counter})
        counter += 1
    return attached, {"nodeRows": counter, "outOfRange": out_of_range, "standalone": standalone}


def _link_length(nodes: Sequence[dict[str, Any]], link: dict[str, Any]) -> float:
    """The 3-D distance between the two RAW origins -- no hull offset -- as `0x102ff3e0` sums."""
    return math.dist(nodes[int(link["src"])]["origin"]["source"],
                     nodes[int(link["dst"])]["origin"]["source"])


def wander_caps(nodes: Sequence[dict[str, Any]], links: Sequence[dict[str, Any]],
                used_hull_bits: int) -> list[dict[str, Any]]:
    """Per hull bit set in `UsedHullBits`: `20 x median` link length over the links whose motion
    word for that hull is non-zero. A declared hull with no link takes the human figure and says
    so; a graph with no human link either answers 0.0, which admits no place."""

    def lengths(hull: int) -> list[float]:
        return [_link_length(nodes, link) for link in links
                if 1 + hull < len(link["fields"]) and int(link["fields"][1 + hull])]

    human = lengths(HUMAN_HULL)
    human_cap = WANDER_GUARD_HOPS * statistics.median(human) if human else 0.0
    caps = []
    for hull in range(RETAIL_HULL_COUNT):
        if not used_hull_bits & (1 << hull):
            continue
        own = lengths(hull)
        if own:
            caps.append({"hull": hull, "capUnits": WANDER_GUARD_HOPS * statistics.median(own),
                         "fromHuman": False})
        else:
            caps.append({"hull": hull, "capUnits": human_cap, "fromHuman": True})
    return caps


def crosswalk_pairs(links: Sequence[dict[str, Any]],
                    attached: dict[int, tuple[int, int]]) -> list[list[int]]:
    """Every link joining two nodes bound to crosswalk hints, lower index first, in link order,
    each pair once."""
    crossing = {node for node, (_, hint_type) in attached.items()
                if hint_type == CROSSWALK_HINT_TYPE}
    pairs: list[list[int]] = []
    seen: set[tuple[int, int]] = set()
    for link in links:
        src, dst = int(link["src"]), int(link["dst"])
        if src in crossing and dst in crossing and src != dst:
            pair = (min(src, dst), max(src, dst))
            if pair not in seen:
                seen.add(pair)
                pairs.append(list(pair))
    return pairs


def _place_row(node: dict[str, Any], hint: int) -> dict[str, Any]:
    offsets = [float(value) for value in node.get("hullOffsets") or []]
    origin = [float(value) for value in node["origin"]["source"]]
    if len(origin) != 3 or len(offsets) != RETAIL_HULL_COUNT:
        raise MapPlacesError(f"AIN node {node.get('index')} has an invalid position/hull-offset table")
    if not all(math.isfinite(value) for value in (*origin, *offsets, float(node["yaw"]))):
        raise MapPlacesError(f"AIN node {node.get('index')} is not finite")
    if node.get("type") is None or node.get("flags") is None:
        raise MapPlacesError(f"AIN node {node.get('index')} carries no type/flags "
                             "(re-export: uv run elysium export_v2 nav-graph-glb <map>)")
    wc_id = node.get("wcId")
    return {
        "index": int(node["index"]),
        "type": int(node["type"]),
        "flags": int(node["flags"]),
        # The raw origin: `GetPosition`'s hull arm is the runtime's, which is why every offset rides.
        "origin": [round(float(c), 5) for c in source_to_unreal(*origin)],
        # A pure yaw about Z under the Y reflection (`source_angles_to_unreal_quat`, M R M with
        # M = diag(1,-1,1)) is the same rotation negated: what the hint actors' rotator reads.
        "yaw": 0.0 - float(node["yaw"]),
        "zOffsets": [round(value * INCH_TO_CM, 5) for value in offsets],
        "wcId": -1 if wc_id is None else int(wc_id),
        "hint": hint,
    }


def stage_rows(map_name: str, entity_rows: Sequence[dict[str, Any]],
               block: dict[str, Any]) -> dict[str, Any]:
    """The `places` block for one map, from its entity rows and its nav-graph extension block."""
    header = block["header"]
    nodes, links = list(block.get("nodes") or []), list(block.get("links") or [])
    num_nodes = int(header["numNodes"])
    if num_nodes != len(nodes) or [int(node["index"]) for node in nodes] != list(range(num_nodes)):
        raise MapPlacesError(f"{map_name}: AIN node table is not a complete index table")
    for link in links:
        if not (0 <= int(link["src"]) < num_nodes and 0 <= int(link["dst"]) < num_nodes):
            raise MapPlacesError(f"{map_name}: AIN link {link.get('index')} names a missing node")
    used_hull_bits = int(header["usedHullBits"]["value"])
    attached, pairing = _pair_hints(map_name, entity_rows, num_nodes)
    return {
        "version": VERSION,
        "map": normalize_key(map_name),
        "numNodes": num_nodes,
        "usedHullBits": used_hull_bits,
        "places": [_place_row(node, attached.get(index, (-1, 0))[0])
                   for index, node in enumerate(nodes)],
        "pairing": pairing,
        "crosswalkPairs": crosswalk_pairs(links, attached),
        "wanderCaps": wander_caps(nodes, links, used_hull_bits),
    }


def pairing_report(payload: dict[str, Any], entity_rows: Sequence[dict[str, Any]]) -> dict[str, int]:
    """How the positional binding compares with the authored ids: per bound node, whether its
    `wcId` equals its hint row's `nodeid`. A disagreement is retail's own pairing, not a defect."""
    pair_blocks = producer.entity_pair_blocks(entity_rows)
    report = {"paired": 0, "agree": 0, "differ": 0, "noNodeid": 0}
    for place in payload["places"]:
        if place["hint"] < 0:
            continue
        report["paired"] += 1
        authored = _folded_get(pair_blocks[place["hint"]], "nodeid")
        if authored is None:
            report["noNodeid"] += 1
        elif atoi(authored) == place["wcId"]:
            report["agree"] += 1
        else:
            report["differ"] += 1
    return report


def stage_for_join(join: Any, map_name: str, root: Path | None = None) -> dict[str, Any]:
    """The block from a prepared `MapJoin` (the map-geometry stage already holds one)."""
    from elysium_pipeline.importers.map_nav_doors import load_graph_block
    return stage_rows(map_name, join.units.entities["entities"], load_graph_block(map_name, root))


def stage_map(map_name: str, root: Path | None = None) -> dict[str, Any]:
    """The block for one map straight from its units, without meshing its geometry."""
    from elysium_pipeline.importers.map_nav_doors import load_graph_block
    units = producer.read_units(map_name, root)
    return stage_rows(map_name, units.entities["entities"], load_graph_block(map_name, root))
