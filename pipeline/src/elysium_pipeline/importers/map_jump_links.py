"""Project decoded retail AIN human jump connections into native map-bake records.

The GLB remains the source product; only the editor reads these centimetre endpoints.
Retail loader 0x102f5bd0 writes link-info then 22 per-hull motion words. InitLinks
0x102fb4e0 sets bit 2 for jumping, and 0x102ff960 rejects link-info bit 0x1000.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path

from elysium_pipeline import paths
from elysium_pipeline.formats.bsp import source_to_unreal
from elysium_pipeline.formats.nav_graph_glb.model import NAV_GRAPH_EXTENSION, normalize_key
from elysium_pipeline.formats.unit_contract import read_glb

RECIPE_VERSION = 1
HUMAN_HULL = 0
RETAIL_HULL_COUNT = 22
MOVE_JUMP = 2
LINK_OFF = 0x1000  # VtMB 0x102ff960; the later Source SDK uses a different bit.
NODE_GROUND = 2


def _endpoint(node: dict, hull: int) -> list[float]:
    # CAI_Node::GetPosition 0x102fb0d0 adds m_flVOffset[hull] for ground nodes.
    origin = [float(value) for value in node["origin"]["source"]]
    if len(origin) != 3 or len(node.get("hullOffsets", [])) != RETAIL_HULL_COUNT:
        raise ValueError("AIN jump endpoint has an invalid position/hull-offset table")
    if not node.get("tail") or int(node["tail"][0]) != NODE_GROUND:
        raise ValueError("AIN jump connection names a non-ground node")
    origin[2] += float(node["hullOffsets"][hull])
    if not all(math.isfinite(value) for value in origin):
        raise ValueError("AIN jump endpoint is not finite")
    return list(source_to_unreal(*origin))


def project_graph(block: dict, map_name: str) -> dict:
    """Select the shipped human capsule's jump links, with one shared bidirectional edge.

    AIN stores one link in both nodes (0x102f626f/0x102f629f). Directional jump legality is
    checked on traversal in retail (0x102ff960), so src/dst are not one-way instructions.
    Malformed graph data fails the bake instead of clamping an endpoint to another node.
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
        if not int(header["usedHullBits"]["value"]) & (1 << HUMAN_HULL) or not int(fields[1 + HUMAN_HULL]) & MOVE_JUMP:
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
                         bidirectional=True))
    result = dict(version=RECIPE_VERSION, map=normalize_key(map_name), hull=HUMAN_HULL,
                  sourceLinks=len(links), excluded=excluded, links=rows)
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
