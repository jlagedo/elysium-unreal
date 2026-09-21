"""The navigation gate: judge a map's baked meshes against retail's own graph.

The graph is the only machine-readable record of where retail's NPCs could walk, so it is the
only way to judge a baked mesh by something other than looking at it. Every ground link must path
on its own agent's mesh; every jump endpoint must project; and the links only one agent has must
path on that agent's mesh -- the claim a one-mesh port cannot make. Findings already judged are
pinned in `nav_known_findings.py`; anything new fails, and so does a pin that stops reproducing.

It lives here, beside the verdicts it calls, rather than in the CLI, because two callers need it
and only one of them is a command: `verify nav` asks on demand, and `bake map` asks of every mesh
it has just built. The lane that builds the meshes must not be able to finish having built a
wrong one -- a gate nobody runs is not a gate. Before 0018 story 21-2 the automatic caller was
`import map-collision`, which the fold retired.
"""

from __future__ import annotations

from collections.abc import Callable, Sequence
from dataclasses import dataclass
import json
from pathlib import Path

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
        reports[key["map"]] = report
        failed += report["failed"]
        if on_line is not None:
            line = ", ".join(f"{row['check']} {row['failed']}" for row in rows if row["failed"])
            pinned = sum(len(row.get("knownFindings", [])) for row in rows)
            flight = sum(row.get("flightClaimCount", 0) for row in rows)
            note = (f"{len(excused)} step-height outlier(s) excused, "
                    f"{pinned} pinned finding(s) reproduced")
            if flight:
                note += f", {flight} flight claim(s) reported"
            on_line(f"  {key['map']}: {'clean' if report['clean'] else line}  [{note}]")
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
