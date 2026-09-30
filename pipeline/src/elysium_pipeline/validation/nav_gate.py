"""The navigation gate: judge a map's baked meshes against retail's own graph.

The graph is the only machine-readable record of where retail's NPCs could walk, so it is the
only way to judge a baked mesh by something other than looking at it. Every ground link must path
on its own agent's mesh; every jump endpoint must project; the links only one agent has must
path on that agent's mesh -- the claim a one-mesh port cannot make; and every baked jump link must
be present and DISABLED (0018/7: retail plans no jump link, `0x102ff960` step 2), so a bake that
left one enabled -- a route retail does not have -- fails. Findings already judged are
pinned in `nav_known_findings.py`; anything new fails, and so does a pin that stops reproducing.

It lives here, beside the verdicts it calls, rather than in the CLI, because two callers need it
and only one of them is a command: `verify nav` asks on demand, and `bake map` asks of every mesh
it has just built. The lane that builds the meshes must not be able to finish having built a
wrong one -- a gate nobody runs is not a gate. Before 0018 story 21-2 the automatic caller was
`import map-collision`, which the fold retired.

Beside the checks it writes the place reports (0018 story 4) into each map's `observations`:
places and authored points off each agent's mesh, authored points no node covers, and the AIN
zones against each mesh's connectivity. They are asked in the same editor call and never fail the
gate; pinning them is 0018/21-9's.
"""

from __future__ import annotations

from collections.abc import Callable, Sequence
from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any

#: How far a path may exceed the straight line before it is reported. One constant, read by
#: `verify nav --factor`'s default and by the bake's automatic run, so the two cannot drift.
DEFAULT_FACTOR = 3.0


@dataclass(frozen=True)
class NavGateResult:
    """What one run of the gate judged."""

    maps: int
    failed: int
    report_path: Path


def report_path(work_root: Path) -> Path:
    """Where the gate leaves its verdicts, for an error message to name."""
    return work_root / "verify" / "nav" / "report.json"


def baked_jump_link_errors(expected: dict[str, Any],
                           found: Sequence[dict[str, Any]] | None) -> dict[str, Any]:
    """The level's jump-link records against the key's `bakedJumpLinks` (0018/7).

    Each staged pair must be present once, with no agent, a disabled smart link and nothing else
    traversable. Retail refuses every jump-only link at `0x102ff960` step 2 (no NPC holds
    `bits_CAP_MOVE_JUMP`), so an enabled record is a route retail does not have: it changes what
    routes exist and so event order. A missing record is a lost piece of the graph's record; an
    unexpected one is a bake the key does not describe. Per-hull verdict counts ride along as the
    bake reported them, and a record claiming a usable capability fails too.
    """

    from elysium_pipeline.validation import nav_acceptance as verdicts

    failures: list[dict[str, Any]] = []
    if found is None:
        found = []
        failures.append({"reason": "verify nav answered no jump-link records (stale editor script?)"})
    wanted = [int(index) for index in expected["expected"]]
    seen: dict[int, int] = {}
    per_hull: dict[str, dict[str, int]] = {}
    for row in found:
        index = int(row["index"])
        seen[index] = seen.get(index, 0) + 1
        if index not in wanted:
            failures.append({"index": index, "reason": "a jump-link record the key does not stage"})
        if row.get("enabled") or int(row.get("agentBits", 0)) or row.get("traversable"):
            failures.append({"index": index, "enabled": bool(row.get("enabled")),
                             "agentBits": int(row.get("agentBits", 0)),
                             "reason": "the jump link is traversable; retail refuses every jump "
                                       "at 0x102ff960 step 2"})
        for verdict in row.get("verdicts", []):
            counts = per_hull.setdefault(str(verdict["hull"]), dict(
                records=0, jumpOnly=0, usable=0, legalForward=0, legalBack=0))
            counts["records"] += 1
            counts["jumpOnly"] += int(bool(verdict["jumpOnly"]))
            counts["usable"] += int(bool(verdict["capabilityUsable"]))
            counts["legalForward"] += int(bool(verdict["legalForward"]))
            counts["legalBack"] += int(bool(verdict["legalBack"]))
            if verdict["capabilityUsable"]:
                failures.append({"index": index, "hull": verdict["hull"],
                                 "reason": "a record claims an NPC can hold the jump capability"})
    for index in wanted:
        if index not in seen:
            failures.append({"index": index, "reason": "the staged jump link has no record"})
        elif seen[index] > 1:
            failures.append({"index": index, "count": seen[index],
                             "reason": "the jump link is recorded more than once"})
    listed = failures[:verdicts.MAX_LISTED]
    return {
        "check": "baked-jump-links-disabled",
        "failed": len(failures),
        "failures": listed,
        "truncated": len(failures) - len(listed),
        "expected": len(wanted),
        "present": len(found),
        "disabled": sum(1 for row in found if not row.get("enabled")),
        "verdictsByHull": per_hull,
        "graph": (expected.get("summary") or {}).get("graph"),
    }


def jump_reach_report(jump_rows: Sequence[dict[str, Any]], lengths: Sequence[float] | None,
                      hull: int) -> dict[str, Any]:
    """The jump-only pairs this hull's mesh WALKS -- a report row, never a failure (0018/7).

    Retail has no edge for a hull across a jump-only pair: `InitLinks 0x102fb4e0` wrote 2 because
    its ground walk failed there, and `0x102ff960` step 2 refuses the jump for every NPC. The
    mesh may still join the two ends (hub link 731: both ends at the same height, 630 cm apart),
    which is the named NavMesh modernization reaching further than retail. Measured per map here
    so the over-reach is a number; `failed` is always 0.
    """

    from elysium_pipeline.validation import nav_acceptance as verdicts

    check = f"jump-only-walked-hull-{hull}"
    only = [(row, length) for row, length in zip(jump_rows, lengths or [])
            if int(row.get("motion", 2)) == 2]
    if lengths is None:
        return {"check": check, "failed": 0, "failures": [], "truncated": 0, "report": True,
                "hull": hull, "unavailable": "verify nav asked no jump-only path lengths"}
    walked = [{"index": int(row["index"]), "src": int(row["src"]), "dst": int(row["dst"]),
               "straightCm": round(float(row["straightCm"]), 1), "pathCm": round(float(length), 1)}
              for row, length in only if length >= 0]
    return {
        "check": check, "failed": 0, "failures": [], "truncated": 0, "report": True,
        "hull": hull,
        "jumpOnly": len(only),
        "walked": len(walked),
        "walkedIds": [row["index"] for row in walked],
        "walkedPairs": walked[:verdicts.MAX_LISTED],
        "noPath": sum(1 for _, length in only if length == verdicts.NO_PATH),
        "offMesh": sum(1 for _, length in only if length == verdicts.OFF_MESH),
    }


def judge(
    config,
    runner,
    maps: Sequence[str],
    *,
    factor: float = DEFAULT_FACTOR,
    on_line: Callable[[str], None] | None = None,
) -> NavGateResult:
    """Run the gate over `maps` and write its report; `on_line` prints the per-map summary."""
    from elysium_pipeline import unreal
    from elysium_pipeline.importers import map_nav_acceptance as acceptance
    from elysium_pipeline.validation import nav_acceptance as verdicts
    from elysium_pipeline.validation.nav_known_findings import known_detours

    root = report_path(config.work_root).parent
    root.mkdir(parents=True, exist_ok=True)
    key_path, answers_path = root / "key.json", root / "answers.json"
    answers_path.unlink(missing_ok=True)

    keys = []
    for name in maps:
        key = acceptance.stage(name)
        key["agentNames"] = acceptance.agent_names(key["hulls"])
        keys.append(key)
    key_path.write_text(json.dumps({"maps": keys}), encoding="utf-8")

    unreal.verify_nav(config, runner, key_path, answers_path)
    if not answers_path.is_file():
        raise RuntimeError(f"verify nav produced no answers at {answers_path}")
    answered = json.loads(answers_path.read_text(encoding="utf-8"))

    by_map = {row["map"]: row for row in answered["maps"]}
    reports, failed = {}, 0
    for key in keys:
        answer = by_map.get(key["map"])
        if answer is None or answer.get("skipped"):
            raise RuntimeError(
                f"{key['map']}: {answer.get('skipped') if answer else 'no answer'}")
        rows = [verdicts.mesh_errors(
            [key["agentNames"][str(hull)] for hull in key["hulls"]], answer["meshes"])]
        excused = {row["index"] for row in key["stepOutliers"]}
        for hull, table in key["perHull"].items():
            given = answer["perHull"].get(hull)
            if given is None:
                continue
            ground = verdicts.ground_link_errors(
                table["ground"], given["groundLengths"], int(hull),
                factor=factor, excused=excused,
                known=known_detours(key["map"], int(hull)))
            starts = verdicts.projection_errors(
                "jump-start", table["jump"], given["jumpStartsLanded"], int(hull))
            ends = verdicts.projection_errors(
                "jump-end", table["jump"], given["jumpEndsLanded"], int(hull))
            if int(hull) in verdicts.FLYING_HULLS:
                # The hull's NPC flies, so its nodes stand in the air and none of these three
                # is a claim about a walkable surface. Reported, never failed.
                rows.extend(verdicts.flight_row(row, int(hull))
                            for row in (ground, starts, ends))
            else:
                rows.extend((ground, starts, ends))
                # 0018/7: where the mesh walks what retail cannot. Reported, never failed.
                rows.append(jump_reach_report(table["jump"], given.get("jumpLengths"), int(hull)))
        # 0018/7: the baked jump links are records, never routes.
        rows.append(baked_jump_link_errors(key["bakedJumpLinks"], answer.get("jumpLinks")))
        for hull, given in answer.get("bridging", {}).items():
            bridging = verdicts.bridging_errors(
                key["agentOnly"][hull]["bridging"], given["agentLengths"],
                given["baseLengths"], int(hull), key["baseHull"])
            if int(hull) in verdicts.FLYING_HULLS:
                rows.append(verdicts.flight_row(bridging, int(hull)))
            else:
                rows.append(bridging)
        report = verdicts.report(rows)
        report["stepOutliers"] = len(excused)
        # The place reports (0018 story 4): observations beside the checks, never counted by
        # `report`, so nothing in them can fail the gate.
        observations = verdicts.place_observations(
            key.get("placeQueries") or {"unavailable": "the key carries no place queries"},
            answer.get("places", {}), key["agentNames"])
        report["observations"] = observations
        reports[key["map"]] = report
        failed += report["failed"]
        if on_line is not None:
            line = ", ".join(f"{row['check']} {row['failed']}" for row in rows if row["failed"])
            pinned = sum(len(row.get("knownFindings", [])) for row in rows)
            flight = sum(row.get("flightClaimCount", 0) for row in rows)
            walked = sum(row.get("walked", 0) for row in rows if row.get("report"))
            note = (f"{len(excused)} step-height outlier(s) excused, "
                    f"{pinned} pinned finding(s) reproduced")
            if flight:
                note += f", {flight} flight claim(s) reported"
            note += f", {walked} jump-only pair(s) the mesh walks"
            on_line(f"  {key['map']}: {'clean' if report['clean'] else line}  [{note}]")
            on_line(f"    {verdicts.place_summary(observations)}  [observations, unpinned]")
    out = report_path(config.work_root)
    out.write_text(json.dumps(reports, indent=2), encoding="utf-8")
    # A per-map copy beside it, because `report.json` holds only the LAST run: `bake map` judges
    # one map per launch, so a corpus pass overwrites each map's verdicts with the next map's and
    # a failure found early cannot be read once the pass has moved on (0018 story 21-8).
    by_map = out.parent / "by-map"
    by_map.mkdir(parents=True, exist_ok=True)
    for name, report in reports.items():
        (by_map / f"{name}.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return NavGateResult(maps=len(keys), failed=failed, report_path=out)
