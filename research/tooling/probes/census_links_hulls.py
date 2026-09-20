# -*- coding: utf-8 -*-
"""Census of the shipped node graphs BY HULL -- which hulls carry links, and where a rat differs.

Reads the graphs **retail actually loads**: the patch's own loose `maps/graphs/*.ain`, which the
engine resolves ahead of the packed copies in the VPKs. The pipeline's own index deliberately
prunes that directory (`formats/install.RUNTIME_CACHE_DIRS`) because the running game rewrites
`.loc` stamps there and would invalidate every export receipt, so this probe opens the files
directly and decodes them with the seam's own reader rather than a second parser.

Reports, over every graph that parses:

  * links carrying motion per hull, and the motion values seen (1 ground, 2 jump);
  * `UsedHullBits` by map, and the gap between DECLARING a hull and carrying a link for it;
  * per witness map and per hull, the link and component counts the acceptance harness pins;
  * the rat-only links -- those with rat motion and no human motion -- and, of those, the ones
    joining node sets the human links keep apart, which is what "a rat passes where a human
    cannot" has to mean for a mesh to be judged against it.

Read-only unless ``--json`` is supplied; generated reports belong below
``$ELYSIUM_WORK_ROOT/research`` and are never committed.

Usage::

    uv run elysium research census_links_hulls
    uv run elysium research census_links_hulls --map sp_tutorial_1
    uv run elysium research census_links_hulls --json <external-path>
"""
from __future__ import annotations

import argparse
import collections
import glob
import json
import os

from elysium_pipeline.formats import install
from elysium_pipeline.formats.nav_graph_glb import decode as nav_decode
from elysium_pipeline.formats.nav_graph_glb.model import asset_id
from elysium_pipeline.formats.nav_graph_glb.source import NavGraphSourceClosure
from elysium_pipeline.formats.unit_contract import Origin, SourceMember
from elysium_pipeline.paths import repo_root

#: Retail's hull count and the two motion bits any shipped link ever carries.
EXPECTED_HULLS = 22
MOVE_GROUND, MOVE_JUMP = 1, 2

#: The link row: `src`, `dst`, then `info` and one motion word per hull.
LINK_INFO_FIELD = 0
LINK_MOTION_BASE = 1

#: The two hulls both witness maps declare (`UsedHullBits 0x80001`).
HUMAN_HULL, RAT_HULL = 0, 19

WITNESSES = ("sp_tutorial_1", "sm_hub_1")


class _UnionFind:
    def __init__(self, size: int):
        self.parent = list(range(size))

    def find(self, item: int) -> int:
        while self.parent[item] != item:
            self.parent[item] = self.parent[self.parent[item]]
            item = self.parent[item]
        return item

    def union(self, left: int, right: int) -> None:
        left, right = self.find(left), self.find(right)
        if left != right:
            self.parent[right] = left


def graphs_dir() -> str:
    return os.path.join(install.PATCH, "maps", "graphs")


def read_graph(path: str):
    """Decode one loose `.ain` with the seam's reader, bypassing the pruned install index."""

    with open(path, "rb") as handle:
        data = handle.read()
    key = os.path.basename(path)[: -len(".ain")]
    origin = Origin(kind="loose", root=os.path.dirname(path))
    member = SourceMember(role="ain", path=f"maps/graphs/{key}.ain", data=data, origin=origin)
    closure = NavGraphSourceClosure(
        key=key, asset_id=asset_id(key), ain=member, loc=None, map_resolved=False)
    return nav_decode.decode_nav_graph(closure)


def motion(link, hull: int) -> int:
    """The motion word this link carries for `hull`, or 0 when the row is too short."""

    index = LINK_MOTION_BASE + hull
    return link.fields[index] if index < len(link.fields) else 0


def components(node_count: int, links, hull: int) -> list[int]:
    """Component sizes over the nodes this hull's links actually touch, largest first."""

    union = _UnionFind(node_count)
    touched: set[int] = set()
    for link in links:
        if motion(link, hull):
            union.union(link.src, link.dst)
            touched.update((link.src, link.dst))
    sizes = collections.Counter(union.find(node) for node in touched)
    return sorted(sizes.values(), reverse=True)


def rat_only(model) -> dict:
    """Rat links with no human motion, and those bridging separate human components."""

    human = _UnionFind(len(model.nodes))
    for link in model.links:
        if motion(link, HUMAN_HULL):
            human.union(link.src, link.dst)
    only, bridging = [], []
    for link in model.links:
        if not motion(link, RAT_HULL) or motion(link, HUMAN_HULL):
            continue
        only.append((link.src, link.dst))
        if human.find(link.src) != human.find(link.dst):
            bridging.append((link.src, link.dst))
    return {"ratOnly": len(only), "bridging": bridging}


def survey(paths: list[str]) -> dict:
    hull_links = collections.Counter()
    hull_motions: dict[int, collections.Counter] = collections.defaultdict(collections.Counter)
    declared = collections.Counter()          # hull -> maps DECLARING it
    carrying = collections.Counter()          # hull -> maps carrying a link for it
    bits_histogram = collections.Counter()
    per_map: dict[str, dict] = {}
    nodes = edges = empty = 0
    failed: list[str] = []

    for path in sorted(paths):
        name = os.path.basename(path)[: -len(".ain")]
        try:
            model = read_graph(path)
        except Exception as error:                                  # noqa: BLE001
            failed.append(f"{name}: {error!r}"[:120])
            continue
        bits = model.header.used_hull_bits.value
        bits_histogram[bits] += 1
        for hull in range(EXPECTED_HULLS):
            if bits & (1 << hull):
                declared[hull] += 1
        if not model.nodes:
            empty += 1
            continue
        nodes += len(model.nodes)
        edges += len(model.links)
        local = collections.Counter()
        for link in model.links:
            for hull in range(EXPECTED_HULLS):
                word = motion(link, hull)
                if word:
                    hull_links[hull] += 1
                    hull_motions[hull][word] += 1
                    local[hull] += 1
        for hull in local:
            carrying[hull] += 1
        per_map[name] = {
            "usedHullBits": f"{bits:#x}",
            "nodes": len(model.nodes),
            "links": len(model.links),
            "linksByHull": dict(sorted(local.items())),
        }
        if name in WITNESSES:
            per_map[name]["perHull"] = {
                hull: {
                    "links": local.get(hull, 0),
                    "ground": sum(1 for link in model.links if motion(link, hull) == MOVE_GROUND),
                    "jump": sum(1 for link in model.links if motion(link, hull) == MOVE_JUMP),
                    "components": components(len(model.nodes), model.links, hull),
                }
                for hull in (HUMAN_HULL, RAT_HULL)
            }
            per_map[name]["rat"] = rat_only(model)

    return {
        "graphs": len(paths),
        "parsed": len(paths) - len(failed),
        "empty": empty,
        "nodes": nodes,
        "links": edges,
        "linksByHull": dict(sorted(hull_links.items())),
        "motionsByHull": {hull: dict(counter) for hull, counter in sorted(hull_motions.items())},
        "mapsDeclaringHull": dict(sorted(declared.items())),
        "mapsCarryingHullLinks": dict(sorted(carrying.items())),
        "usedHullBits": {f"{bits:#x}": count for bits, count in bits_histogram.most_common()},
        "perMap": per_map,
        "failed": failed,
    }


def hull_names() -> dict[int, str]:
    path = repo_root() / "docs" / "vtmb" / "data" / "hull_table.json"
    if not path.exists():
        return {}
    return {row["bit"]: row["name"] for row in json.loads(path.read_text(encoding="utf-8"))["rows"]}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="census_links_hulls", add_help=False)
    parser.add_argument("--map", action="append", dest="maps", default=None)
    parser.add_argument("--json", dest="json_path", default=None)
    args = parser.parse_args(argv)

    directory = graphs_dir()
    if not os.path.isdir(directory):
        print(f"FAIL: {directory} does not exist")
        return 1
    paths = ([os.path.join(directory, f"{name}.ain") for name in args.maps] if args.maps
             else glob.glob(os.path.join(directory, "*.ain")))

    report = survey(paths)
    names = hull_names()
    print(f"{report['parsed']} of {report['graphs']} graphs parsed, {report['empty']} empty: "
          f"{report['nodes']} nodes, {report['links']} links")
    for line in report["failed"]:
        print(f"   parse failed: {line}")

    print("\nlinks with motion, per hull:")
    for hull, count in report["linksByHull"].items():
        declaring = report["mapsDeclaringHull"].get(hull, 0)
        carrying = report["mapsCarryingHullLinks"].get(hull, 0)
        gap = "" if declaring == carrying else f"  (declared by {declaring}, carried by {carrying})"
        print(f"   hull {hull:2d} {names.get(hull, '?'):24s} {count:6d} links  "
              f"motions {dict(report['motionsByHull'][hull])}{gap}")

    print("\nUsedHullBits by map count:")
    print("   " + ", ".join(f"{bits} {count}" for bits, count in report["usedHullBits"].items()))

    for name in (args.maps or WITNESSES):
        local = report["perMap"].get(name)
        if local is None or "perHull" not in local:
            continue
        print(f"\n{name}: {local['nodes']} nodes, {local['links']} links, "
              f"UsedHullBits {local['usedHullBits']}")
        for hull, row in local["perHull"].items():
            print(f"   hull {hull:2d} {names.get(hull, '?'):20s} {row['links']:5d} links "
                  f"({row['ground']} ground, {row['jump']} jump) "
                  f"components {row['components']}")
        rat = local["rat"]
        print(f"   rat-only links: {rat['ratOnly']}; bridging separate human node sets: "
              f"{len(rat['bridging'])} {rat['bridging']}")

    if args.json_path:
        target = os.path.abspath(args.json_path)
        if target.startswith(os.fspath(repo_root())):
            print(f"FAIL: {target} is inside the repository")
            return 1
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2)
        print(f"\nwrote {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
