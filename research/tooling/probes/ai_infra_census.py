# -*- coding: utf-8 -*-
"""Census of the retail AI infrastructure entities and AIN nav graphs in the V2 export.

Walks every ``<export root>/maps/<map>.entities.glb`` and ``<export root>/nav-graphs/<map>.glb``
unit and reports, per map and in total: nav-graph size, hull-0 jump links, connectivity, and
the authored counts of the AI infrastructure classnames: hint nodes (any ``info_node_*``/``info_hint``
row carrying ``hinttype`` — retail spells the interesting places ``intersting_place``, one ``e``),
graph nodes, makers, plus the key/value surface each counted classname carries.

Read-only unless ``--json`` is supplied.  Generated reports belong below
``$ELYSIUM_WORK_ROOT/research`` and must not be committed.

Usage::

    uv run elysium research ai_infra_census
    uv run elysium research ai_infra_census --json <external-path>
"""
from __future__ import annotations

import argparse
import collections
import json
import os

from elysium_pipeline.formats.map_entities_glb.model import MAP_ENTITIES_EXTENSION
from elysium_pipeline.formats.nav_graph_glb.model import NAV_GRAPH_EXTENSION
from elysium_pipeline.formats.unit_contract import read_glb
from elysium_pipeline.paths import export_v2_root

#: The interesting places, spelled as retail spells them (`intersting`, one ``e``).
PLACE_CLASSNAME = "intersting_place"
CONVERSATION_CLASSNAME = "intersting_place_conversation"
PATROL_CLASSNAME = "info_node_patrol_point"

#: The three maker classnames, folded.
MAKER_CLASSNAMES = frozenset({"npc_maker", "npc_maker_fleshpile", "npc_maker_zombie"})
MAKER_TYPE_KEY = "npctype"
SQUAD_MEMBER_KEY = "squadname"
MAKER_SQUAD_KEY = "npcsquadname"

#: A hint node is any ``info_node_*``/``info_hint`` row carrying ``hinttype``; an ``info_node*``
#: row without it is a graph node, not a hint. Plain ``info_node`` is always a graph node.
HINT_KEY = "hinttype"
HINT_PREFIX = "info_node_"
HINT_BARE = "info_hint"
GRAPH_BARE = "info_node"

#: Hull-0 motion word bit 2 is jump: InitLinks 0x102fb4e0 (see importers/map_jump_links.py).
MOVE_JUMP = 2

#: The entity row's key/value pair list. Read off the first row of the first entity unit
#: (``list(row.keys())`` -> ``keyValues``, items carrying ``key`` and ``value``).
PAIR_LIST_KEY = "keyValues"

#: The nav link's two node-index keys. Read off a link dict of a nav unit that has links:
#: ``src``/``dst`` (the exporter's ``_link_json``).
LINK_ENDPOINT_KEYS = ("src", "dst")

#: Table B example values must stay under this length to be quotable in ``docs/vtmb``.
EXAMPLE_VALUE_LIMIT = 40


class _UnionFind:
    """Union-find over ``0..size-1`` node indices."""

    def __init__(self, size: int):
        self.parent = list(range(size))

    def find(self, item: int) -> int:
        parent = self.parent
        while parent[item] != item:
            parent[item] = parent[parent[item]]
            item = parent[item]
        return item

    def union(self, left: int, right: int) -> None:
        left_root, right_root = self.find(left), self.find(right)
        if left_root != right_root:
            self.parent[right_root] = left_root


def read_entity_rows(path) -> list:
    document, _ = read_glb(path)
    block = document["extensions"][MAP_ENTITIES_EXTENSION]
    return block["entities"]


def read_nav_block(path) -> dict:
    document, _ = read_glb(path)
    return document["extensions"][NAV_GRAPH_EXTENSION]


def nav_stats(block: dict) -> dict:
    """Size, hull-0 jump links and undirected connectivity of one nav-graph block."""
    header = block["header"]
    node_count = int(header["numNodes"])
    links = block["links"]
    jump_count = 0
    union = _UnionFind(max(node_count, 1))
    for link in links:
        if int(link["fields"][1]) & MOVE_JUMP:
            jump_count += 1
        src, dst = (int(link[key]) for key in LINK_ENDPOINT_KEYS)
        if 0 <= src < node_count and 0 <= dst < node_count:
            union.union(src, dst)
    sizes = collections.Counter()
    for node in block["nodes"]:
        index = int(node["index"])
        if 0 <= index < node_count:
            sizes[union.find(index)] += 1
    component_sizes = sorted(sizes.values(), reverse=True)
    return {
        "nodes": node_count,
        "links": len(links),
        "jump_links": jump_count,
        "components": len(component_sizes),
        "component_sizes": component_sizes,
        "largest_components": component_sizes[:5],
    }


def pair_list(row: dict) -> list[tuple[str, str]]:
    """Every key/value pair one entity row authors, key folded; value stringified."""
    pairs = []
    for pair in row.get(PAIR_LIST_KEY) or ():
        value = pair.get("value")
        pairs.append((str(pair.get("key", "")).lower(), "" if value is None else str(value)))
    return pairs


def classify(classname: str, values: dict[str, str]) -> dict[str, bool]:
    """The infrastructure roles one row plays; a patrol point is a hint and a patrol point."""
    has_hinttype = HINT_KEY in values
    is_hint = (classname.startswith(HINT_PREFIX) or classname == HINT_BARE) and has_hinttype
    return {
        "hint": is_hint,
        "place": classname == PLACE_CLASSNAME,
        "conversation": classname == CONVERSATION_CLASSNAME,
        "patrol": classname == PATROL_CLASSNAME,
        "maker": classname in MAKER_CLASSNAMES,
        # A squad is not a placed helper entity: it is materialised by NPC and maker keyfields.
        # Keep those rows in the census so their complete authored key surface is not discarded.
        "squad": bool(values.get(SQUAD_MEMBER_KEY) or values.get(MAKER_SQUAD_KEY)),
        "graph_node": (classname == GRAPH_BARE
                       or (classname.startswith(HINT_PREFIX) and not has_hinttype)),
    }


def _number(text: str) -> float | None:
    try:
        return float(text)
    except ValueError:
        return None


def build_report() -> dict:
    root = export_v2_root()
    entity_paths = sorted((root / "maps").glob("*.entities.glb"))
    nav_paths = sorted((root / "nav-graphs").glob("*.glb"))

    maps: dict[str, dict] = {}
    for path in entity_paths:
        stem = path.name[: -len(".entities.glb")]
        maps.setdefault(stem.lower(), {"map": stem})["entities"] = read_entity_rows(path)
    for path in nav_paths:
        stem = path.name[: -len(".glb")]
        maps.setdefault(stem.lower(), {"map": stem})["nav"] = nav_stats(read_nav_block(path))

    # Global aggregates for tables B, C, D, E and E2.
    class_keys: dict[tuple[str, str], dict] = {}
    hint_types: dict[tuple[str, str], dict] = {}
    place_groups: dict[str, dict] = {}
    patrol_keys: set[str] = set()
    conversation_keys: dict[str, int] = collections.Counter()
    maker_types: dict[tuple[str, str], dict] = {}
    squad_names: dict[str, dict] = {}
    squad_map_rows: dict[tuple[str, str], dict] = {}

    for entry in maps.values():
        counts = collections.Counter()
        entry_squads: set[str] = set()
        for row in entry.get("entities", ()):
            classname = (row.get("classname") or "").lower()
            pairs = pair_list(row)
            values = dict(pairs)
            roles = classify(classname, values)
            if not any(roles.values()):
                continue
            for role, hit in roles.items():
                if hit:
                    counts[role] += 1
            for key, value in pairs:
                bucket = class_keys.setdefault((classname, key), {"rows": 0, "values": []})
                bucket["rows"] += 1
                bucket["values"].append(value)
            if roles["hint"]:
                bucket = hint_types.setdefault(
                    (classname, values.get(HINT_KEY, "")), {"rows": 0, "maps": set()})
                bucket["rows"] += 1
                bucket["maps"].add(entry["map"].lower())
            if roles["place"]:
                bucket = place_groups.setdefault(
                    values.get("group_id", ""),
                    {"rows": 0, "maps": set(), "on": 0, "off": 0, "min_time": None,
                     "max_time": None})
                bucket["rows"] += 1
                bucket["maps"].add(entry["map"].lower())
                if values.get("enabled") == "1":
                    bucket["on"] += 1
                elif values.get("enabled") == "0":
                    bucket["off"] += 1
                seconds = _number(values.get("min_time", ""))
                if seconds is not None:
                    bucket["min_time"] = (seconds if bucket["min_time"] is None
                                          else min(bucket["min_time"], seconds))
                seconds = _number(values.get("max_time", ""))
                if seconds is not None:
                    bucket["max_time"] = (seconds if bucket["max_time"] is None
                                          else max(bucket["max_time"], seconds))
            if roles["patrol"]:
                patrol_keys.update(values)
            if roles["conversation"]:
                conversation_keys.update(values.keys())
            if roles["maker"]:
                npc_type = values.get(MAKER_TYPE_KEY, "")
                bucket = maker_types.setdefault(
                    (entry["map"].lower(), npc_type),
                    {"rows": 0, "classnames": set()},
                )
                bucket["rows"] += 1
                bucket["classnames"].add(classname)
            for key, kind in ((SQUAD_MEMBER_KEY, "placed"),
                              (MAKER_SQUAD_KEY, "maker_requests")):
                squad_name = values.get(key, "").strip()
                if not squad_name:
                    continue
                folded_name = squad_name.lower()
                entry_squads.add(folded_name)
                bucket = squad_names.setdefault(
                    folded_name,
                    {"name": squad_name, "maps": set(), "placed": 0, "maker_requests": 0},
                )
                bucket["maps"].add(entry["map"].lower())
                bucket[kind] += 1
                map_bucket = squad_map_rows.setdefault(
                    (entry["map"].lower(), folded_name),
                    {"name": squad_name, "placed": 0, "maker_requests": 0},
                )
                map_bucket[kind] += 1
                counts["squad_members" if kind == "placed" else "maker_squad_requests"] += 1
        entry["counts"] = counts
        entry["squads"] = entry_squads

    report = {
        "entity_units": len(entity_paths),
        "nav_units": len(nav_paths),
        "maps": [],
        "class_keys": [],
        "hint_types": [],
        "place_groups": [],
        "patrol_keys": sorted(patrol_keys),
        "conversation_keys": [],
        "maker_types": [],
        "squad_names": [],
        "squad_map_rows": [],
    }
    for stem in sorted(maps, key=lambda key: maps[key]["map"].lower()):
        entry = maps[stem]
        counts = entry.get("counts", collections.Counter())
        nav = entry.get("nav")
        report["maps"].append({
            "map": entry["map"],
            "nodes": nav["nodes"] if nav else None,
            "links": nav["links"] if nav else None,
            "jump_links": nav["jump_links"] if nav else None,
            "components": nav["components"] if nav else None,
            "largest_components": nav["largest_components"] if nav else None,
            "component_sizes": nav["component_sizes"] if nav else None,
            "graph_nodes": counts["graph_node"],
            "hints": counts["hint"],
            "places": counts["place"],
            "conversations": counts["conversation"],
            "patrol_points": counts["patrol"],
            "makers": counts["maker"],
            "squad_members": counts["squad_members"],
            "maker_squad_requests": counts["maker_squad_requests"],
            "squads": len(entry.get("squads", ())),
        })
    for (classname, key), bucket in sorted(class_keys.items()):
        distinct = sorted(set(bucket["values"]))
        report["class_keys"].append({
            "classname": classname,
            "key": key,
            "rows": bucket["rows"],
            "distinct_values": len(distinct),
            "examples": [value for value in distinct if len(value) < EXAMPLE_VALUE_LIMIT][:5],
        })
    for (classname, hinttype), bucket in sorted(
            hint_types.items(), key=lambda item: (item[0][0], -item[1]["rows"], item[0][1])):
        report["hint_types"].append({
            "classname": classname,
            "hinttype": hinttype,
            "rows": bucket["rows"],
            "maps": len(bucket["maps"]),
        })
    for group, bucket in sorted(place_groups.items(), key=lambda item: (-item[1]["rows"], item[0])):
        report["place_groups"].append({
            "group_id": group,
            "rows": bucket["rows"],
            "maps": len(bucket["maps"]),
            "enabled_1": bucket["on"],
            "enabled_0": bucket["off"],
            "min_time": bucket["min_time"],
            "max_time": bucket["max_time"],
        })
    report["conversation_keys"] = [
        {"key": key, "rows": rows} for key, rows in sorted(conversation_keys.items())]
    for (map_name, npc_type), bucket in sorted(maker_types.items()):
        report["maker_types"].append({
            "map": map_name,
            "npc_type": npc_type,
            "rows": bucket["rows"],
            "classnames": sorted(bucket["classnames"]),
        })
    report["squad_names"] = [
        {
            "squad": bucket["name"],
            "maps": len(bucket["maps"]),
            "map_names": sorted(bucket["maps"]),
            "placed_members": bucket["placed"],
            "maker_requests": bucket["maker_requests"],
        }
        for _, bucket in sorted(squad_names.items())
    ]
    report["squad_map_rows"] = [
        {
            "map": map_name,
            "squad": bucket["name"],
            "placed_members": bucket["placed"],
            "maker_requests": bucket["maker_requests"],
        }
        for (map_name, _), bucket in sorted(squad_map_rows.items())
    ]
    return report


def _cell(value) -> str:
    text = "" if value is None else str(value)
    return text.replace("|", "\\|").replace("\n", " ")


def _md_table(headers: list[str], rows: list[list]) -> list[str]:
    lines = ["| " + " | ".join(headers) + " |",
             "| " + " | ".join("---" for _ in headers) + " |"]
    for row in rows:
        lines.append("| " + " | ".join(_cell(cell) for cell in row) + " |")
    return lines


def print_report(report: dict) -> None:
    print(f"_Computed by `uv run elysium research ai_infra_census` over "
          f"{report['entity_units']} entity units and {report['nav_units']} "
          "nav-graph units under the V2 export root._")
    print()
    print("### Per map")
    totals = {
        "nodes": 0, "links": 0, "jump_links": 0, "components": 0,
        "graph_nodes": 0, "hints": 0, "places": 0, "conversations": 0,
        "patrol_points": 0, "makers": 0, "squad_members": 0,
        "maker_squad_requests": 0, "squads": 0,
    }
    pool: list[int] = []
    rows = []
    for entry in report["maps"]:
        has_nav = entry["nodes"] is not None
        if has_nav:
            totals["nodes"] += entry["nodes"]
            totals["links"] += entry["links"]
            totals["jump_links"] += entry["jump_links"]
            totals["components"] += entry["components"]
            pool.extend(entry["component_sizes"])
        totals["hints"] += entry["hints"]
        totals["graph_nodes"] += entry["graph_nodes"]
        totals["places"] += entry["places"]
        totals["conversations"] += entry["conversations"]
        totals["patrol_points"] += entry["patrol_points"]
        totals["makers"] += entry["makers"]
        totals["squad_members"] += entry["squad_members"]
        totals["maker_squad_requests"] += entry["maker_squad_requests"]
        totals["squads"] += entry["squads"]
        rows.append([
            entry["map"],
            entry["nodes"] if has_nav else "-",
            entry["links"] if has_nav else "-",
            entry["jump_links"] if has_nav else "-",
            entry["components"] if has_nav else "-",
            ", ".join(str(size) for size in entry["largest_components"]) if has_nav else "-",
            entry["graph_nodes"],
            entry["hints"],
            entry["places"],
            entry["conversations"],
            entry["patrol_points"],
            entry["makers"],
            entry["squad_members"],
            entry["maker_squad_requests"],
            entry["squads"],
        ])
    rows.append([
        "totals",
        totals["nodes"], totals["links"], totals["jump_links"], totals["components"],
        ", ".join(str(size) for size in sorted(pool, reverse=True)[:5]),
        totals["graph_nodes"], totals["hints"], totals["places"], totals["conversations"],
        totals["patrol_points"], totals["makers"], totals["squad_members"],
        totals["maker_squad_requests"], totals["squads"],
    ])
    for line in _md_table(
            ["map", "nodes", "links", "jump links", "components", "five largest",
             "entity graph nodes", "hint nodes", "places", "conversation places",
             "info_node_patrol_point", "makers", "squad members", "maker squad requests",
             "squads (per map)"],
            rows):
        print(line)

    print()
    print("### Keys authored per infrastructure classname")
    rows = [
        [entry["classname"], entry["key"], entry["rows"], entry["distinct_values"],
         " / ".join(entry["examples"])]
        for entry in report["class_keys"]
    ]
    for line in _md_table(
            ["classname", "key", "rows", "distinct values", "examples (<=5, sorted)"], rows):
        print(line)

    print()
    print("### Hint types")
    rows = [
        [entry["classname"], entry["hinttype"], entry["rows"], entry["maps"]]
        for entry in report["hint_types"]
    ]
    for line in _md_table(["classname", "hinttype", "rows", "maps"], rows):
        print(line)

    print()
    print("### Interesting-place groups")
    rows = [
        [entry["group_id"], entry["rows"], entry["maps"], entry["enabled_1"], entry["enabled_0"],
         entry["min_time"], entry["max_time"]]
        for entry in report["place_groups"]
    ]
    for line in _md_table(
            ["group_id", "rows", "maps", "enabled=1", "enabled=0", "min_time", "max_time"], rows):
        print(line)

    print()
    print("### Keys authored on `info_node_patrol_point`")
    for line in _md_table(["key authored on info_node_patrol_point"],
                          [[key] for key in report["patrol_keys"]]):
        print(line)

    print()
    print("### Conversation places")
    rows = [[entry["key"], entry["rows"]] for entry in report["conversation_keys"]]
    for line in _md_table(["key on intersting_place_conversation", "rows"], rows):
        print(line)

    print()
    print("### Makers by `NPCType`")
    rows = [
        [entry["map"], entry["npc_type"], entry["rows"], ", ".join(entry["classnames"])]
        for entry in report["maker_types"]
    ]
    for line in _md_table(["map", "NPCType", "maker requests", "maker classnames"], rows):
        print(line)

    print()
    print("### Squads by map")
    rows = [
        [entry["map"], entry["squad"], entry["placed_members"], entry["maker_requests"]]
        for entry in report["squad_map_rows"]
    ]
    for line in _md_table(
            ["map", "squad", "placed members", "maker requests"], rows):
        print(line)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", help="write the complete generated ledger here")
    args = parser.parse_args()
    report = build_report()
    print_report(report)
    if args.json:
        parent = os.path.dirname(os.path.abspath(args.json))
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(args.json, "w") as handle:
            json.dump(report, handle, indent=1, sort_keys=True)


if __name__ == "__main__":
    main()
