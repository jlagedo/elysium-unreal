"""Roll one finalized capture's integrity and unjoined counts into one report.

Usage:
    uv run elysium research verify_capture_integrity <session> [<session> ...]

CAP2.7 asks a run to report the same integrity counts as CAP1.1 plus its
unjoined-record counts. Every verifier already computes a shard of the second
question -- a contribution outside a pose build, an actor address constructed
but never posed, a scene binding that reaches no renderable -- but each names it
differently, measures it against its own denominator, and reports it alone.
Nothing reads more than one of them.

This tool imports each verifier and reads the report it already produces. It
runs no query of its own: re-deriving thirty predicates here would fork them
from the verifiers that own them, and the first correction to any one of those
would silently stop applying.

**It never sums across denominators.** The shards count different populations --
records, identities, generations, scopes, bindings -- so a single "unjoined"
total would be a plausible number that means nothing. A bucket aggregates how
many of its shards fired, never their counts.

A shard whose non-zero value is a known property of the runtime rather than a
defect carries ``expected_nonzero`` and the reason it holds. It is reported with
that reason and never folded into a pass. An explicit unknown is an acceptable
answer; a lost join is not.

`index_capture_database` stores what this returns, so the database reports its
own counts without the verifiers being re-run to learn them. That pass is the
writer; this tool computes the roll-up and must not gain one, because
re-deriving fifty predicates beside the verifiers that own them would fork from
them and the first correction to any one would silently stop applying.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any, Callable

from research.tooling.capture.calibrate_theatre_capture import (
    calibrate,
    compared_maps,
    resolve_session,
)
from research.tooling.capture.verify_actor_identity_lifetime import (
    verify as verify_actors,
)
from research.tooling.capture.verify_consumed_spans import verify as verify_spans
from research.tooling.capture.verify_entity_pointer_join import verify as verify_join
from research.tooling.capture.verify_model_skeleton_census import (
    verify as verify_census,
)
from research.tooling.capture.verify_pose_build_generation import (
    verify as verify_generation,
)
from research.tooling.capture.verify_scene_requests import verify as verify_scene
from research.tooling.capture.verify_source_attribution import (
    verify as verify_attribution,
)

REPORT_NAME = "capture-integrity.json"


@dataclass(frozen=True)
class Shard:
    """One verifier's count of a population it could not join or account for."""

    tool: str
    name: str
    path: tuple[str, ...]
    bucket: str
    of: tuple[str, ...] | None = None
    expected_nonzero: bool = False
    because: str = ""
    # The verifier's own support flag for the stream this count needs. Not
    # every verifier marks an unjudged section with available False -- some
    # emit a reduced dict instead -- so which streams a shard needs is stated
    # here rather than inferred from the shape of what came back.
    requires: str | None = None


CONTRIBUTIONS = "carries_contributions"
SCENE = "carries_scene"
SEQUENCE_CHANGES = "carries_sequence_changes"


# Every count below is already computed by the verifier named in `tool`; this
# table only says where it lives, what it is measured against, and whether a
# non-zero value is a defect or a known property of the runtime.
SHARDS: tuple[Shard, ...] = (
    # --- the writer's own accounting -------------------------------------
    Shard("calibrate", "records dropped by the hook",
          ("integrity", "hook", "dropped"), "records"),
    Shard("calibrate", "callbacks reaching no readable header or pose buffer",
          ("integrity", "failures", "hook_skipped", "count"), "records"),
    Shard("calibrate", "records removed by the configured filter",
          ("integrity", "failures", "hook_filtered", "count"), "records"),
    Shard("calibrate", "streams with a torn tail",
          ("integrity", "failures", "incomplete_streams", "count"), "streams"),
    Shard("calibrate", "gaps in the global sequence counter",
          ("integrity", "sequence_numbers", "gaps"), "records"),
    Shard("calibrate", "duplicate sequence numbers",
          ("integrity", "sequence_numbers", "duplicates"), "records"),
    # --- pose-build grouping ---------------------------------------------
    Shard("generation", "records assigned to no generation",
          ("coverage", "unassigned"), "records", ("coverage", "records")),
    Shard("generation", "records naming a bracket the run never opened",
          ("integrity", "records_naming_a_missing_bracket"), "records",
          ("coverage", "records")),
    Shard("generation", "brackets whose parent is missing",
          ("integrity", "brackets_with_a_missing_parent"), "generations"),
    Shard("generation", "evaluations outside their bracket's span",
          ("integrity", "evaluations_outside_their_bracket_span"), "records"),
    Shard("generation", "evaluations recorded on another thread",
          ("integrity", "evaluations_on_another_thread"), "records"),
    Shard("generation", "pose builds spanning several entities",
          ("pose_groups", "pose_builds_spanning_several_entities"),
          "generations"),
    Shard("generation", "pose builds whose evaluations name another entity",
          ("pose_groups", "pose_builds_whose_evaluations_name_another_entity"),
          "generations"),
    Shard("generation", "pose builds that produced no evaluation",
          ("brackets", "pose_builds_without_evaluations"), "generations",
          ("brackets", "pose_builds"), True,
          "A bone cache answers a second SetupBones for an actor already posed "
          "this frame, so the call builds nothing. CAP2.1 measured 181,751 of "
          "419,700."),
    Shard("generation", "draws submitted by a frame that built no pose",
          ("cross_check", "draws_without_a_pose_build"), "records",
          ("cross_check", "draws"), True,
          "Same bone cache seen from the draw side. Reported as having no pose "
          "build rather than attributed to an earlier one. CAP2.1 measured "
          "146,247 of 395,785."),
    # --- model census ------------------------------------------------------
    Shard("census", "used header identities never observed",
          ("coverage", "identities_unobserved"), "identities",
          ("coverage", "identities_used")),
    Shard("census", "header identities observed after first use",
          ("coverage", "identities_observed_late"), "identities",
          ("coverage", "identities_used")),
    Shard("census", "header identities observed but never used",
          ("coverage", "identities_observed_but_unused"), "identities",
          None, True,
          "An include-model bank is censused by the contribution path and "
          "animates no actor of its own, so it is observed without being used "
          "as an entity model."),
    Shard("census", "captured images with no installed counterpart",
          ("source_join", "unresolved"), "identities"),
    # --- actor identity and lifetime --------------------------------------
    Shard("actors", "actor identities never observed",
          ("coverage", "identities_unobserved"), "identities",
          ("coverage", "identities_used")),
    Shard("actors", "actor identities observed after first use",
          ("coverage", "identities_observed_late"), "identities",
          ("coverage", "identities_used")),
    Shard("actors", "records outside the interval their address named",
          ("lifetime", "records_outside_their_interval"), "records"),
    Shard("actors", "identity intervals still open at capture stop",
          ("lifetime", "intervals_open_at_capture_stop"), "identities",
          None, True,
          "The run stops mid-scene, so every actor still alive has an interval "
          "the capture never saw closed."),
    Shard("actors", "records outside any witnessed lifetime of their address",
          ("lifetime", "records_outside_any_lifetime"), "records"),
    Shard("actors", "posed entities with no construction",
          ("lifetime", "entities_without_a_construction"), "identities"),
    Shard("actors", "addresses constructed but never posed",
          ("lifetime", "constructions_of_entities_never_posed"), "addresses",
          None, True,
          "The hookable construction pair is C_BaseEntity, shared by every "
          "client entity, so most constructions are not actors. CAP2.3 counted "
          "562 apart."),
    # --- entity pointer join ----------------------------------------------
    Shard("join", "skeletal entities resolving to no drawn entity",
          ("verdict", "unresolved_animation_entities"), "identities",
          ("verdict", "animation_entities")),
    # No denominator: the report carries the shared-model count and the two
    # stream-only counts separately, never a per-stream total, so any ratio
    # here would divide by the wrong population.
    Shard("join", "models drawn without any skeletal evaluation",
          ("delta", "pose_only_models"), "identities",
          None, True,
          "A prop or piece of scenery is drawn and never posed. CAP1.3 "
          "measured 49 of 367 drawn entities with a skeletal counterpart; the "
          "rest are this population seen at model granularity."),
    # --- source attribution ------------------------------------------------
    Shard("attribution", "contributions outside every pose build",
          ("coverage", "unbracketed"), "records", ("coverage", "sequences")),
    Shard("attribution", "decoded cells outside every contribution scope",
          ("coverage", "unscoped"), "records", ("coverage", "cells")),
    Shard("attribution", "scopes carrying cells but no sequence",
          ("coverage", "orphan_cells"), "scopes", ("coverage", "scopes")),
    Shard("attribution", "scopes spanning several generations",
          ("coverage", "scopes_spanning_generations"), "scopes",
          ("coverage", "scopes")),
    Shard("attribution", "contributions whose generation built no pose",
          ("coverage", "outside_a_pose_build"), "records"),
    Shard("attribution", "evaluated pose builds naming no source",
          ("coverage", "generations_without_a_contribution"), "generations",
          ("coverage", "evaluation_generations")),
    Shard("attribution", "owner identities used without a census observation",
          ("owners", "unobserved_count"), "identities",
          ("owners", "identities")),
    Shard("attribution", "owner identities observed late",
          ("owners", "late_count"), "identities", ("owners", "identities")),
    Shard("attribution", "owner identities carrying no image",
          ("owners", "without_image_count"), "identities",
          ("owners", "identities")),
    Shard("attribution", "contributions whose owner carries no image to range",
          ("indices", "unresolved"), "records", ("indices", "checked")),
    Shard("attribution", "indices outside their owner's declared counts",
          ("indices", "out_of_range_count"), "records", ("indices", "checked")),
    Shard("attribution", "descriptor pointers disagreeing with the stride",
          ("indices", "misplaced_count"), "records", ("indices", "checked")),
    Shard("attribution", "caller addresses resolving to no spec function",
          ("callers", "unresolved_records"), "records"),
    Shard("attribution", "sequences carrying no witnessed blend weight",
          ("blends", "unwitnessed"), "records", ("blends", "sequences")),
    Shard("attribution", "scopes decoding other than the cells they declare",
          ("declared_cells", "disagreeing"), "scopes",
          ("declared_cells", "scopes_checked")),
    Shard("attribution", "contributions carrying a copy fault",
          ("faults", "records_with_a_fault"), "records"),
    # --- consumed spans ----------------------------------------------------
    Shard("spans", "contributions whose spans could not be resolved",
          ("spans", "faulted"), "records"),
    Shard("spans", "span sets falling outside the image they name",
          ("spans", "span_sets_outside_their_image"), "scopes"),
    Shard("spans", "cells whose mask selects a bone no decoder witnessed",
          ("witness", "unwitnessed_with_a_selecting_mask"), "records",
          ("witness", "cells")),
    # --- scene requests ----------------------------------------------------
    Shard("scene", "scene records outside every request scope",
          ("coverage", "unscoped"), "records", requires=SCENE),
    Shard("scene", "dispatched events whose scene never started",
          ("coverage", "events_without_a_started_scene"), "records",
          requires=SCENE),
    Shard("scene", "scene actors resolving to no indexed entity",
          ("binding", "bindings_unresolved"), "bindings", requires=SCENE),
    Shard("scene", "anim-set bindings reaching no observed renderable",
          ("join", "anim_set_owner", "without_a_renderable"), "bindings",
          ("join", "anim_set_owner", "bindings"), requires=SCENE),
    Shard("scene", "anim-set bindings whose actor animated under another model",
          ("join", "anim_set_owner", "disagreeing"), "bindings",
          ("join", "anim_set_owner", "compared"), requires=SCENE),
    Shard("scene", "sequence changes joining no scene binding",
          ("requests", "unattributed"), "records",
          ("requests", "sequence_changes"), True,
          "An idle NPC, the player and a scripted sequence all change sequence "
          "without a choreographed scene asking. CAP2.6 measured 280,857 "
          "uncovered actor resolutions reproduced exactly across two runs.",
          requires=SEQUENCE_CHANGES),
    Shard("scene", "sequence changes outside every pose build",
          ("requests", "outside_a_pose_build"), "records",
          requires=SEQUENCE_CHANGES),
    Shard("scene", "truncated scene strings",
          ("truncation", "truncated_records"), "records", requires=SCENE),
)


def _tools(join_source: bool) -> dict[str, Callable[[Path], dict[str, Any]]]:
    return {
        "calibrate": calibrate,
        "generation": verify_generation,
        "census": lambda session: verify_census(session, join_source=join_source),
        "actors": verify_actors,
        "join": verify_join,
        "attribution": verify_attribution,
        "spans": verify_spans,
        "scene": verify_scene,
    }


SIDECARS = {
    "calibrate": "calibration.json",
    "generation": "pose-build-generation.json",
    "census": "model-skeleton-census.json",
    "actors": "actor-identity-lifetime.json",
    "join": "entity-join.json",
    "attribution": "source-attribution.json",
    "spans": "consumed-spans-verdict.json",
    "scene": "scene-requests-verdict.json",
}


def resolve(report: dict[str, Any] | None, path: tuple[str, ...]) -> tuple[Any, str]:
    """Read one shard's value out of a verifier report.

    Three outcomes are deliberately distinct. A whole section absent or
    ``None`` means the verifier did not judge it, because the stream it reads is
    not in this capture: unavailable, not zero. A key missing *inside* a
    section the verifier did produce means this table names something the
    verifier no longer reports, which has to be loud -- a silently dropped
    shard is a population that reads as accounted for.

    Counts arrive in three shapes: an integer, a list whose length is the count
    (an offender or gap list), and a decimal string, because the hook's own
    counters travel through ``done.txt`` as text.
    """
    if report is None:
        return None, "tool-unavailable"
    section = report.get(path[0])
    if section is None:
        return None, "unavailable"
    node: Any = section
    for step in path[1:]:
        if node is None:
            return None, "unavailable"
        # The verifiers mark a section they could not judge with available
        # False and a reason, rather than omitting it. That is a stream this
        # capture does not carry, not a key this table got wrong.
        if isinstance(node, dict) and node.get("available") is False:
            return None, "unavailable"
        if not isinstance(node, dict) or step not in node:
            return None, "missing"
        node = node[step]
    if node is None:
        return None, "unavailable"
    if isinstance(node, list):
        return len(node), "present"
    if isinstance(node, str):
        try:
            return int(node), "present"
        except ValueError:
            return None, "missing"
    if isinstance(node, bool) or not isinstance(node, int):
        return None, "missing"
    return node, "present"


def aggregate(
    session: Path,
    join_source: bool = True,
    reports: dict[str, dict[str, Any]] | None = None,
) -> dict[str, Any]:
    if reports is None:
        reports = {}
        for name, run in _tools(join_source).items():
            reports[name] = run(session)

    rows: list[dict[str, Any]] = []
    for shard in SHARDS:
        report = reports.get(shard.tool)
        supported = (
            True
            if shard.requires is None
            else bool(((report or {}).get("support") or {}).get(shard.requires))
        )
        count, status = (
            resolve(report, shard.path) if supported else (None, "unavailable")
        )
        denominator, _ = (
            resolve(report, shard.of)
            if shard.of and supported
            else (None, "present")
        )
        rows.append(
            {
                "tool": shard.tool,
                "name": shard.name,
                "path": ".".join(shard.path),
                "bucket": shard.bucket,
                "status": status,
                "count": count,
                "of": denominator,
                "ratio": (
                    None
                    if count is None or not denominator
                    else round(count / denominator, 6)
                ),
                "expected_nonzero": shard.expected_nonzero,
                "because": shard.because or None,
            }
        )

    buckets: dict[str, dict[str, int]] = {}
    for row in rows:
        # Counts are never added across shards: they measure different
        # populations, so a total would be a plausible number meaning nothing.
        entry = buckets.setdefault(
            row["bucket"], {"shards": 0, "available": 0, "nonzero": 0}
        )
        entry["shards"] += 1
        if row["status"] == "present":
            entry["available"] += 1
            if row["count"]:
                entry["nonzero"] += 1

    unexpected = [
        row
        for row in rows
        if row["status"] == "present"
        and row["count"]
        and not row["expected_nonzero"]
    ]
    accounted = [
        row
        for row in rows
        if row["status"] == "present" and row["count"] and row["expected_nonzero"]
    ]
    missing = [row for row in rows if row["status"] == "missing"]
    unavailable = [
        row for row in rows if row["status"] in ("unavailable", "tool-unavailable")
    ]

    calibration = reports.get("calibrate") or {}
    written = (calibration.get("volume") or {}).get("bytes_written")
    verdict = {
        "table_intact": not missing,
        "integrity_complete": not unexpected and not missing,
        "shards_declared": len(rows),
        "shards_available": sum(1 for row in rows if row["status"] == "present"),
    }
    if missing:
        verdict["statement"] = (
            f"{len(missing)} declared shards name a key their verifier no "
            "longer reports, so this roll-up is not counting what it claims: "
            + ", ".join(f"{row['tool']}.{row['path']}" for row in missing[:6])
        )
    elif unexpected:
        verdict["statement"] = (
            f"{len(unexpected)} population"
            + ("s are" if len(unexpected) != 1 else " is")
            + " unaccounted for: "
            + "; ".join(
                f"{row['name']} = {row['count']}"
                + (f" of {row['of']}" if row["of"] else "")
                for row in unexpected[:6]
            )
        )
    else:
        verdict["statement"] = (
            f"Every one of the {verdict['shards_available']} available shards "
            "is zero except "
            f"{len(accounted)} whose non-zero value is a recorded property of "
            "the runtime, and each of those is reported with the reason it "
            f"holds. {len(unavailable)} shards read a stream this capture does "
            "not carry."
        )
    return {
        "session": session.name,
        "session_path": str(session),
        "identity": {
            "map": ((calibration.get("identity") or {}).get("map")),
            "created_utc": (calibration.get("identity") or {}).get("created_utc"),
            "tool_git": (calibration.get("identity") or {}).get("tool_git"),
            "modules": (calibration.get("identity") or {}).get("modules"),
        },
        "written": {
            "bytes_written": written,
            "queue_peak": (calibration.get("volume") or {}).get("queue_peak"),
            "records": sum(
                entry["records"]
                for entry in (calibration.get("volume") or {}).get("per_kind", [])
            ),
        },
        "shards": rows,
        "by_bucket": dict(sorted(buckets.items())),
        "unexpected_nonzero": unexpected,
        "accounted_nonzero": accounted,
        "missing_paths": missing,
        "unavailable": [
            {"tool": row["tool"], "path": row["path"], "status": row["status"]}
            for row in unavailable
        ],
        "verdict": verdict,
    }


def compare(reports: list[dict[str, Any]]) -> dict[str, Any]:
    same_map, maps = compared_maps(reports)
    rows = [
        {
            "session": report["session"],
            "map": (report.get("identity") or {}).get("map"),
            "shards_available": report["verdict"]["shards_available"],
            "unexpected_nonzero": len(report["unexpected_nonzero"]),
            "integrity_complete": report["verdict"]["integrity_complete"],
        }
        for report in reports
    ]
    failing = [row["session"] for row in rows if not row["integrity_complete"]]
    if failing:
        statement = (
            "Runs disagree because " + ", ".join(failing) + " leave a "
            "population unaccounted for."
        )
    else:
        statement = (
            f"All {len(rows)} runs account for every population they measure"
            + (
                " across different scenes (" + ", ".join(maps) + "), which the "
                "shards' own denominators make a per-run claim rather than a "
                "comparison of what was played."
                if not same_map
                else "."
            )
        )
    return {"rows": rows, "maps": maps, "same_map": same_map, "statement": statement}


def summarize(report: dict[str, Any]) -> str:
    verdict = report["verdict"]
    lines = [
        f"session      {report['session']}  map={report['identity']['map']}",
        f"shards       {verdict['shards_available']} available of "
        f"{verdict['shards_declared']} declared; table_intact="
        f"{verdict['table_intact']}",
    ]
    for bucket, entry in report["by_bucket"].items():
        lines.append(
            f"  {bucket:<12} {entry['available']:>2} available  "
            f"{entry['nonzero']:>2} non-zero"
        )
    for row in report["unexpected_nonzero"]:
        lines.append(
            f"  UNACCOUNTED  {row['tool']}.{row['path']} = {row['count']}"
            + (f" of {row['of']}" if row["of"] else "")
        )
    for row in report["accounted_nonzero"]:
        lines.append(
            f"  accounted    {row['tool']}.{row['path']} = {row['count']}"
            + (f" of {row['of']}" if row["of"] else "")
        )
    for row in report["missing_paths"]:
        lines.append(f"  MISSING PATH {row['tool']}.{row['path']}")
    lines.append(f"verdict      {verdict['statement']}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--report", type=Path)
    parser.add_argument(
        "--no-session-reports",
        action="store_true",
        help=f"Do not write {REPORT_NAME} beside each database.",
    )
    parser.add_argument(
        "--no-source-join",
        action="store_true",
        help="Skip the census source join, which reads the installed models.",
    )
    parser.add_argument(
        "--reuse-reports",
        action="store_true",
        help=(
            "Read each verifier's sidecar beside the database instead of "
            "re-running it. Errors if one is absent or older than the database."
        ),
    )
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        cached = None
        if args.reuse_reports:
            cached = {}
            database = session / "capture.sqlite"
            for name, sidecar in SIDECARS.items():
                path = session / sidecar
                if not path.is_file():
                    raise FileNotFoundError(
                        f"--reuse-reports needs {path}; run {name} first"
                    )
                if database.is_file() and path.stat().st_mtime < database.stat().st_mtime:
                    raise ValueError(
                        f"{path} is older than {database}, so it describes an "
                        "earlier capture"
                    )
                cached[name] = json.loads(path.read_text(encoding="utf-8"))
        report = aggregate(
            session,
            join_source=not args.no_source_join,
            reports=cached,
        )
        reports.append(report)
        if not args.no_session_reports:
            (session / REPORT_NAME).write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8"
            )
        print(summarize(report), flush=True)

    combined = {
        "sessions": [report["session"] for report in reports],
        "reports": reports,
        "comparison": compare(reports) if len(reports) > 1 else None,
    }
    if combined["comparison"]:
        print(combined["comparison"]["statement"], flush=True)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(combined, indent=2) + "\n", encoding="utf-8"
        )
        print(args.report.resolve(), flush=True)
    return (
        0
        if all(report["verdict"]["integrity_complete"] for report in reports)
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
