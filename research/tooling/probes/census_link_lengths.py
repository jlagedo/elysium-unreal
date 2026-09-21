# -*- coding: utf-8 -*-
"""Census of HOP LENGTH in the shipped node graphs -- how far 20 hops of the random walk reach.

`TASK_GET_PATH_TO_RANDOM_NODE` `0x1f` ends its walk (`0x102ff3e0`) at the resolved distance OR at
the iteration guard `0x14`, accumulating the 3-D distance between RAW node origins
(`docs/vtmb/navigation-jump-links.md` § "`TASK_GET_PATH_TO_RANDOM_NODE` `0x1f`, walked"). Whether
the guard can ever fire ahead of the distance is a property of the link lengths alone, so this
probe measures them: per map and per hull, the median / p10 / p90 hop, `20 x median`, and the
smallest shipped operand that reach falls short of.

Reads the same loose patch graphs as `census_links_hulls`, through its reader. Lengths are Source
units, the unit the schedule operands are written in.

Read-only unless ``--json`` is supplied; generated reports belong below
``$ELYSIUM_WORK_ROOT/research`` and are never committed.

Usage::

    uv run elysium research census_link_lengths
    uv run elysium research census_link_lengths --map sm_hub_1
    uv run elysium research census_link_lengths --hull 19
    uv run elysium research census_link_lengths --json <external-path>
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import os
import statistics

from census_links_hulls import HUMAN_HULL, WITNESSES, graphs_dir, hull_names, motion, read_graph
from elysium_pipeline.paths import repo_root

#: The walk's iteration guard.
GUARD_HOPS = 0x14

#: The 15 issuing schedules' operands, distinct and ascending.
OPERANDS = (200, 256, 500, 1024, 2048, 3000, 4096, 5000)


def percentile(ordered: list[float], fraction: float) -> float:
    """Linear-interpolated percentile of an already sorted list."""

    if len(ordered) == 1:
        return ordered[0]
    position = fraction * (len(ordered) - 1)
    low = math.floor(position)
    high = min(low + 1, len(ordered) - 1)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def hop_lengths(model, hull: int) -> list[float]:
    """The 3-D length of every link carrying motion for `hull`, raw origin to raw origin."""

    lengths = []
    for link in model.links:
        if not motion(link, hull):
            continue
        lengths.append(math.dist(model.nodes[link.src].origin_source,
                                 model.nodes[link.dst].origin_source))
    return lengths


def summarise(lengths: list[float]) -> dict:
    ordered = sorted(lengths)
    median = statistics.median(ordered)
    reach = GUARD_HOPS * median
    return {
        "links": len(ordered),
        "min": ordered[0],
        "p10": percentile(ordered, 0.10),
        "median": median,
        "mean": statistics.fmean(ordered),
        "p90": percentile(ordered, 0.90),
        "max": ordered[-1],
        "guardReach": reach,
        "guardReachP90": GUARD_HOPS * percentile(ordered, 0.90),
        # The smallest shipped operand the guard's median reach falls short of, or None.
        "firstOperandBeyondReach": next((value for value in OPERANDS if value > reach), None),
    }


def survey(paths: list[str], hull: int) -> dict:
    per_map: dict[str, dict] = {}
    pooled: list[float] = []
    failed: list[str] = []
    skipped = 0
    for path in sorted(paths):
        name = os.path.basename(path)[: -len(".ain")]
        try:
            model = read_graph(path)
        except Exception as error:                                  # noqa: BLE001
            failed.append(f"{name}: {error!r}"[:120])
            continue
        lengths = hop_lengths(model, hull)
        if not lengths:
            skipped += 1
            continue
        pooled.extend(lengths)
        per_map[name] = {"nodes": len(model.nodes), **summarise(lengths)}
    return {
        "hull": hull,
        "graphs": len(paths),
        "skipped": skipped,
        "failed": failed,
        "corpus": summarise(pooled) if pooled else None,
        "perMap": per_map,
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="census_link_lengths", add_help=False)
    parser.add_argument("--map", action="append", dest="maps", default=None)
    parser.add_argument("--hull", type=int, default=HUMAN_HULL)
    parser.add_argument("--json", dest="json_path", default=None)
    args = parser.parse_args(argv)

    directory = graphs_dir()
    if not os.path.isdir(directory):
        print(f"FAIL: {directory} does not exist")
        return 1
    paths = ([os.path.join(directory, f"{name}.ain") for name in args.maps] if args.maps
             else glob.glob(os.path.join(directory, "*.ain")))

    report = survey(paths, args.hull)
    for line in report["failed"]:
        print(f"   parse failed: {line}")
    if report["corpus"] is None:
        print(f"no graph carries a link for hull {args.hull}")
        return 1

    name = hull_names().get(args.hull, "?")
    print(f"hull {args.hull} {name}: {len(report['perMap'])} maps with links, "
          f"{report['skipped']} without; lengths in Source units; guard = {GUARD_HOPS} hops")
    print(f"operands: {', '.join(map(str, OPERANDS))}\n")

    header = (f"{'map':24s} {'nodes':>5s} {'links':>6s} {'p10':>6s} {'median':>7s} {'p90':>6s} "
              f"{'max':>6s} {'20xmed':>7s} {'20xp90':>7s}  guard fires before")
    print(header)
    rows = [("CORPUS", {"nodes": 0, **report["corpus"]}), *sorted(report["perMap"].items())]
    for map_name, row in rows:
        beyond = row["firstOperandBeyondReach"]
        mark = " *" if map_name in WITNESSES else ""
        print(f"{map_name:24s} {row['nodes'] or '':>5} {row['links']:6d} {row['p10']:6.0f} "
              f"{row['median']:7.0f} {row['p90']:6.0f} {row['max']:6.0f} "
              f"{row['guardReach']:7.0f} {row['guardReachP90']:7.0f}  "
              f"{'never' if beyond is None else beyond}{mark}")

    print("\nmaps by the smallest operand the guard's median reach falls short of:")
    tally: dict = {}
    for row in report["perMap"].values():
        tally[row["firstOperandBeyondReach"]] = tally.get(row["firstOperandBeyondReach"], 0) + 1
    for operand in (*OPERANDS, None):
        if operand in tally:
            print(f"   {'never' if operand is None else operand:>6}: {tally[operand]} maps")

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
