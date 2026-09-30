"""Project decoded retail AIN jump connections into native map-bake records.

The GLB remains the source product; only the editor reads these centimetre endpoints.
Retail loader 0x102f5bd0 writes link-info then 22 per-hull motion words. InitLinks
0x102fb4e0 sets bit 2 for jumping, and 0x102ff960 rejects link-info bit 0x1000.

**No jump link is ever planned in retail** (0018/7). The link predicate `0x102ff960` ANDs the
NPC's capabilities (slot 513 `CapabilitiesGet`, `+0x804/4`, `m_afCapability` `+0x5cec`) with the
link's per-hull word (`link+0x0c+4*hull`, `102ff98d`) and refuses a zero result (`102ff995`); only
an answer of exactly 2 (`102ff9c8 CMP [ESP+0x10],2`) reaches `IsJumpLegal` (slot 521, `+0x824/4`).
No shipped NPC holds bit 2 (`bits_CAP_MOVE_JUMP`): every `CapabilitiesAdd` site passes a literal
without it, the fly toggles `0x1038c170` / `0x103580d0` pass 1 or 4, allocation zeroes the word and
the weapon's slot-360 bits (0, 0x2000, 0x40018000) carry none. A jump-only word (2) is therefore
refused at step 2 for every NPC on every hull, and step 4 is unreachable.

So what this stages is a RECORD of the graph, not a route: one row per de-duplicated human
(hull 0) jump-only pair, as before, and on each row every used hull's own verdict -- its motion
word, whether it is jump-only there, `capabilityUsable` (always false, with the address that
refuses it) and, for a jump-only hull, that hull's endpoints so the bake can ask the ported
`IsJumpLegal` geometry both ways (`UElysiumNavBakeLibrary::JumpLinkVerdicts`; the geometry is
C++'s, never re-ported here). The bake authors each row as a non-traversable
`AElysiumNavJumpLink`.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path

from elysium_pipeline import paths
from elysium_pipeline.formats.bsp import source_to_unreal
from elysium_pipeline.formats.nav_graph_glb.model import (
    LINK_OFF, NAV_GRAPH_EXTENSION, NODE_GROUND, normalize_key)
from elysium_pipeline.formats.unit_contract import read_glb

#: 2 (0018/7): the rows carry per-hull verdicts and a summary; the bake authors disabled links.
RECIPE_VERSION = 2
HUMAN_HULL = 0
RETAIL_HULL_COUNT = 22
MOVE_GROUND = 1
MOVE_JUMP = 2

#: Whether any shipped NPC can hold `bits_CAP_MOVE_JUMP` (2) in `m_afCapability` (+0x5cec). None
#: can: 63 `CapabilitiesAdd` literals, none with bit 2; `CapabilitiesClear`'s only caller is
#: `CNPC_Crow::Spawn`; the `CAPABILITIES` tweak param (`0x1029aa10`) only prints. The one place the
#: port states it; every `capabilityUsable` below is this AND the word's jump bit.
JUMP_CAPABILITY_HELD = False

#: Why every jump-only word is refused, for the report and the debugger view.
CAPABILITY_REFUSAL = ("0x102ff960 step 2: CapabilitiesGet() (slot 513, +0x5cec) & link word == 0; "
                      "no shipped NPC holds bits_CAP_MOVE_JUMP (2), so IsJumpLegal (slot 521) "
                      "is never reached")


def _endpoint(node: dict, hull: int) -> list[float]:
    # CAI_Node::GetPosition 0x102fb0d0 adds m_flVOffset[hull] for ground nodes.
    origin = [float(value) for value in node["origin"]["source"]]
    if len(origin) != 3 or len(node.get("hullOffsets", [])) != RETAIL_HULL_COUNT:
        raise ValueError("AIN jump endpoint has an invalid position/hull-offset table")
    if node.get("type") != NODE_GROUND:
        raise ValueError("AIN jump connection names a non-ground node")
    origin[2] += float(node["hullOffsets"][hull])
    if not all(math.isfinite(value) for value in origin):
        raise ValueError("AIN jump endpoint is not finite")
    return list(source_to_unreal(*origin))


def capability_usable(word: int) -> bool:
    """`0x102ff960` step 2 for a jump-only word: can any NPC's capability word intersect it?"""
    return bool(word & MOVE_JUMP) and JUMP_CAPABILITY_HELD


def _hull_verdict(link: dict, src: dict, dst: dict, hull: int) -> dict:
    """One used hull's record for one link. Endpoints ride only where the hull's word is jump-only,
    because that is the only word `0x102ff960` would hand to `IsJumpLegal` (step 4)."""
    word = int(link["fields"][1 + hull])
    entry = dict(hull=hull, motion=word, jumpOnly=word == MOVE_JUMP)
    if entry["jumpOnly"]:
        # Forward is src -> dst; the bake asks the reverse too. `0x102ff960` passes (from node,
        # far node, far node) at the pathfinder's hull (`102ffa75`..`102ffaaf`), so the apex the
        # geometry sees IS the far endpoint.
        entry.update(capabilityUsable=capability_usable(word), refusedBy=CAPABILITY_REFUSAL,
                     startCm=_endpoint(src, hull), endCm=_endpoint(dst, hull))
    return entry


def _summary(links: list[dict], hulls: list[int], rows: list[dict]) -> dict:
    """The counting half, over the whole graph and over the recorded rows, per used hull."""
    enabled = [link for link in links if not int(link["fields"][0]) & LINK_OFF]
    graph = {}
    for hull in hulls:
        words = [int(link["fields"][1 + hull]) for link in enabled]
        graph[str(hull)] = dict(
            jumpOnly=sum(1 for word in words if word == MOVE_JUMP),
            # `InitLinks` tries the jump arm only after the walk fails, so no word is ever 3;
            # counted so the day one is, the report says so (a 3 walks and skips step 4).
            groundAndJump=sum(1 for word in words
                              if word & (MOVE_GROUND | MOVE_JUMP) == (MOVE_GROUND | MOVE_JUMP)),
            usable=sum(1 for word in words if word == MOVE_JUMP and capability_usable(word)))
    recorded = {}
    for hull in hulls:
        entries = [entry for row in rows for entry in row["hulls"] if entry["hull"] == hull]
        recorded[str(hull)] = dict(
            jumpOnly=sum(1 for entry in entries if entry["jumpOnly"]),
            usable=sum(1 for entry in entries if entry.get("capabilityUsable")))
    return dict(links=len(rows), graph=graph, recorded=recorded)


def project_graph(block: dict, map_name: str) -> dict:
    """Record the shipped human capsule's jump links, one shared edge per pair, per-hull verdicts.

    AIN stores one link in both nodes (0x102f626f/0x102f629f), so src/dst are not one-way
    instructions and `bidirectional` stays true as data. Malformed graph data fails the bake
    instead of clamping an endpoint to another node.
    """
    header = block["header"]
    if int(header["version"]) != 30 or int(header["numHulls"]) != RETAIL_HULL_COUNT:
        raise ValueError("AIN jump bake requires retail version 30 and 22 hulls")
    nodes, links = block["nodes"], block["links"]
    if int(header["numNodes"]) != len(nodes) or int(header["totalNumLinks"]) != len(links):
        raise ValueError("AIN graph has incomplete node/link tables")
    by_id = {int(node["index"]): node for node in nodes}
    if len(by_id) != len(nodes) or set(by_id) != set(range(len(nodes))):
        raise ValueError("AIN node ids are not a complete unique index table")
    # The agents this map needs a mesh for. `UsedHullBits` is the graph's own answer -- an OR of
    # the hulls its links were built for -- and it is what the bake gives the navigation system as
    # a SupportedAgentsMask, so a map builds the meshes its own graph uses and no others. Both
    # witnesses are `0x80001`: human and rat.
    used_hull_bits = int(header["usedHullBits"]["value"])
    used_hulls = [bit for bit in range(RETAIL_HULL_COUNT) if used_hull_bits & (1 << bit)]
    rows, seen, indices = [], set(), set()
    excluded = dict(nonJump=0, disabled=0, duplicate=0)
    for link in links:
        index, src, dst = (int(link[key]) for key in ("index", "src", "dst"))
        if index in indices:
            raise ValueError(f"duplicate AIN link index {index}")
        indices.add(index)
        fields = link["fields"]
        if len(fields) != RETAIL_HULL_COUNT + 1:
            raise ValueError(f"AIN link {index}: incomplete link-info/hull-motion fields")
        if src not in by_id or dst not in by_id or src == dst:
            raise ValueError(f"AIN link {index}: invalid endpoint {src}->{dst}")
        if int(fields[0]) & LINK_OFF:
            excluded["disabled"] += 1
            continue
        if not used_hull_bits & (1 << HUMAN_HULL) or not int(fields[1 + HUMAN_HULL]) & MOVE_JUMP:
            excluded["nonJump"] += 1
            continue
        pair = tuple(sorted((src, dst)))
        if pair in seen:
            excluded["duplicate"] += 1
            continue
        seen.add(pair)
        rows.append(dict(index=index, src=src, dst=dst, hull=HUMAN_HULL,
                         startCm=_endpoint(by_id[src], HUMAN_HULL),
                         endCm=_endpoint(by_id[dst], HUMAN_HULL),
                         bidirectional=True,
                         hulls=[_hull_verdict(link, by_id[src], by_id[dst], hull)
                                for hull in used_hulls]))
    result = dict(version=RECIPE_VERSION, map=normalize_key(map_name), hull=HUMAN_HULL,
                  usedHullBits=used_hull_bits, usedHulls=used_hulls,
                  sourceLinks=len(links), excluded=excluded, links=rows,
                  summary=_summary(links, used_hulls, rows))
    result["sha256"] = hashlib.sha256(json.dumps(result, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return result


def stage_map(map_name: str, root: Path | None = None) -> dict:
    key = normalize_key(map_name)
    path = (Path(root) if root is not None else paths.export_v2_root()) / "nav-graphs" / (key + ".glb")
    if not path.is_file():
        raise ValueError(f"missing AIN unit {path}; run: uv run elysium export_v2 nav-graph-glb {key}")
    document, _ = read_glb(path)
    block = document.get("extensions", {}).get(NAV_GRAPH_EXTENSION)
    if not isinstance(block, dict):
        raise ValueError(f"{path} carries no {NAV_GRAPH_EXTENSION} extension")
    return project_graph(block, key)
