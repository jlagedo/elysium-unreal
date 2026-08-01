"""Decide whether the draw and skeletal streams share one pointer space.

Usage:
    uv run elysium research verify_entity_pointer_join <session> [<session> ...]

A session is an absolute path or a directory name below
``$ELYSIUM_WORK_ROOT/research/retail-capture/theatre``.  Every database opens
read-only; nothing is written into an evidence file.  Each session receives an
``entity-join.json`` beside its database, and a combined report carries the
cross-run comparison.

The draw stream stores a field loaded out of the StudioRender render info,
while the skeletal streams store the ``C_BaseAnimating`` instance pointer, and
the two sets never share a value.  This report answers CAP1.3: whether the two
are one pointer space separated by a constant delta, and what the distribution
of their difference per model actually is.

The delta is derived from model identity alone.  Timing is reported as a
labelled diagnostic that explains a verdict; it never produces a pairing.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import sqlite3
from typing import Any

from research.tooling.capture.calibrate_theatre_capture import (
    DATABASE_NAME,
    open_database,
    resolve_session,
    stream_headers,
)


POINTER_MASK = 0xFFFFFFFF
POINTER_SPACE = 0x1_0000_0000
# A module image never spans more than this, so a value inside the window is a
# static or global object rather than a heap allocation.
MODULE_IMAGE_WINDOW = 0x0200_0000
# One pair of one model is an accident; a pointer-space rule has to hold over
# several distinct models before it is a join.
MINIMUM_SHARED_MODELS = 2
CROSS_FIELD_SAMPLES = 3
MAX_REPORTED_DELTAS = 16
MAX_REPORTED_DIFFERENCES = 64
MAX_REPORTED_MODELS = 400
MAX_REPORTED_REUSE = 64

POSE_STREAM = "pose"
ANIMATION_STREAM = "animation"


def signed(value: int) -> int:
    """Read a 32-bit pointer difference as the offset a compiler would emit."""
    return value - POINTER_SPACE if value >= 0x8000_0000 else value


def address(value: int) -> str:
    return f"0x{value & POINTER_MASK:08x}"


def entity_sets(connection: sqlite3.Connection) -> dict[str, set[int]]:
    return {
        stream: {
            value
            for (value,) in connection.execute(
                "SELECT DISTINCT client_entity FROM records WHERE stream_name = ?",
                (stream,),
            )
        }
        for (stream,) in connection.execute("SELECT name FROM streams")
    }


def module_bases(headers: dict[str, dict[str, Any]]) -> dict[str, int]:
    return {
        f"{stream}.{key}": int(value, 16)
        for stream, header in headers.items()
        for key, value in header.items()
        if key.endswith("_base")
    }


def geometry(
    values: set[int], bases: dict[str, int]
) -> dict[str, Any]:
    alignment = Counter(value & 0xF for value in values)
    return {
        "distinct": len(values),
        "minimum": address(min(values)) if values else None,
        "maximum": address(max(values)) if values else None,
        "alignment": {
            f"0x{low:x}": count for low, count in sorted(alignment.items())
        },
        # A pointer into a loaded module image is a static object, not one of
        # the per-entity heap allocations the join assumes.
        "inside_module_image": {
            name: sum(
                1 for value in values if base <= value < base + MODULE_IMAGE_WINDOW
            )
            for name, base in bases.items()
        },
    }


def cross_field_sweep(
    connection: sqlite3.Connection,
    pose_entities: set[int],
    animation_entities: set[int],
) -> dict[str, Any]:
    """Test every pointer the draw record already carries against the skeletal set.

    ``draw_arguments`` holds the five raw ``DrawModel`` arguments verbatim and
    has never been examined; if the instance pointer is sitting in one of them
    the join needs no delta at all.
    """
    fields = [
        "client_entity",
        "model_info",
        "studio_hdr",
        *(f"draw_argument_{index}" for index in range(5)),
    ]
    hits: Counter[str] = Counter()
    stable: Counter[str] = Counter()
    examples: dict[str, list[str]] = {}
    for entity in sorted(pose_entities):
        rows = connection.execute(
            """
            SELECT model_info, studio_hdr, draw_arguments FROM records
            WHERE kind = 'POSE' AND client_entity = ?
            ORDER BY qpc LIMIT ?
            """,
            (entity, CROSS_FIELD_SAMPLES),
        ).fetchall()
        matched: Counter[str] = Counter()
        for model_info, studio_hdr, arguments in rows:
            values = dict(
                zip(
                    fields,
                    [
                        entity,
                        model_info,
                        studio_hdr,
                        *(json.loads(arguments) if arguments else ()),
                    ],
                )
            )
            for field, value in values.items():
                if value in animation_entities:
                    matched[field] += 1
                    examples.setdefault(field, [])
                    if len(examples[field]) < 4:
                        examples[field].append(
                            f"{address(entity)}->{address(value)}"
                        )
        for field, count in matched.items():
            hits[field] += 1
            if count == len(rows):
                stable[field] += 1
    return {
        "sampled_entities": len(pose_entities),
        "samples_per_entity": CROSS_FIELD_SAMPLES,
        "fields": [
            {
                "field": field,
                "entities_with_hit": hits[field],
                "entities_stable": stable[field],
                "examples": examples.get(field, []),
            }
            for field in fields
        ],
        "carrying_fields": sorted(field for field in fields if stable[field]),
    }


def model_groups(connection: sqlite3.Connection) -> dict[int, dict[str, Any]]:
    groups: dict[int, dict[str, Any]] = {}
    for checksum, stream, entity, bones in connection.execute(
        """
        SELECT checksum, stream_name, client_entity, max(bone_count)
        FROM records GROUP BY checksum, stream_name, client_entity
        """
    ):
        group = groups.setdefault(
            checksum,
            {
                "checksum": checksum,
                "bone_count": bones,
                "model_name": None,
                POSE_STREAM: set(),
                ANIMATION_STREAM: set(),
            },
        )
        group["bone_count"] = max(group["bone_count"], bones)
        group.setdefault(stream, set()).add(entity)
    for checksum, name in connection.execute(
        "SELECT checksum, model_name FROM records WHERE kind = 'POSE' "
        "GROUP BY checksum"
    ):
        if checksum in groups:
            groups[checksum]["model_name"] = name
    return groups


def differences(group: dict[str, Any]) -> set[int]:
    return {
        (animation - pose) & POINTER_MASK
        for pose in group[POSE_STREAM]
        for animation in group[ANIMATION_STREAM]
    }


def qualify(
    shared: list[dict[str, Any]],
    pose_entities: set[int],
    animation_entities: set[int],
    delta: int,
) -> dict[str, Any]:
    """Score a delta on entity coverage, and report model bijection beside it.

    Coverage is the property that decides the pointer space: every skeletal
    instance has to resolve to a drawn entity.  Per-model bijection is a weaker
    claim about model attribution — an actor can animate under one model and be
    drawn under another — so it is reported rather than required.
    """
    resolved = {
        entity
        for entity in animation_entities
        if (entity - delta) & POINTER_MASK in pose_entities
    }
    bijective = 0
    divergent = []
    for group in shared:
        mapped = {(pose + delta) & POINTER_MASK for pose in group[POSE_STREAM]}
        actual = group[ANIMATION_STREAM]
        if mapped == actual:
            bijective += 1
        elif len(divergent) < MAX_REPORTED_MODELS:
            elsewhere = sum(
                1
                for entity in actual - mapped
                if (entity - delta) & POINTER_MASK in pose_entities
            )
            divergent.append(
                {
                    "model_name": group["model_name"],
                    "checksum": address(group["checksum"]),
                    "pose_entities": len(group[POSE_STREAM]),
                    "animation_entities": len(actual),
                    "matched": len(mapped & actual),
                    # A skeletal actor drawn under another model resolves in
                    # the global sets even though this group is lopsided.
                    "unmatched_drawn_as_another_model": elsewhere,
                }
            )
    return {
        "delta": address(delta),
        "delta_signed": signed(delta),
        "shared_models": len(shared),
        "bijective_models": bijective,
        "animation_entities": len(animation_entities),
        "resolved_animation_entities": len(resolved),
        "covers_all": len(resolved) == len(animation_entities),
        "pose_entities": len(pose_entities),
        "pose_with_counterpart": sum(
            1
            for entity in pose_entities
            if (entity + delta) & POINTER_MASK in animation_entities
        ),
        "models_not_bijective": divergent,
    }


def delta_search(
    groups: dict[int, dict[str, Any]],
    pose_entities: set[int],
    animation_entities: set[int],
) -> dict[str, Any]:
    shared = [
        group
        for group in groups.values()
        if group[POSE_STREAM] and group[ANIMATION_STREAM]
    ]
    support: Counter[int] = Counter()
    for group in shared:
        # One group votes once per delta, so a model with many actors cannot
        # outvote the rest of the cast.
        for delta in differences(group):
            support[delta] += 1
    full = sorted(delta for delta, count in support.items() if count == len(shared))
    leading = [
        {
            "delta": address(delta),
            "delta_signed": signed(delta),
            "models_supporting": count,
            "shared_models": len(shared),
        }
        for delta, count in support.most_common(MAX_REPORTED_DELTAS)
    ]
    return {
        "shared_models": len(shared),
        "pose_only_models": sum(
            1
            for group in groups.values()
            if group[POSE_STREAM] and not group[ANIMATION_STREAM]
        ),
        "animation_only_models": sum(
            1
            for group in groups.values()
            if group[ANIMATION_STREAM] and not group[POSE_STREAM]
        ),
        "distinct_differences": len(support),
        "candidates": [
            qualify(shared, pose_entities, animation_entities, delta)
            for delta in full
        ],
        "leading": leading,
    }


def distribution(
    groups: dict[int, dict[str, Any]], candidates: set[int]
) -> dict[str, Any]:
    shared_rows = []
    single_stream = []
    for group in sorted(
        groups.values(), key=lambda entry: (entry["model_name"] or "", entry["checksum"])
    ):
        pose = group[POSE_STREAM]
        animation = group[ANIMATION_STREAM]
        if not pose or not animation:
            if len(single_stream) < MAX_REPORTED_MODELS:
                single_stream.append(
                    {
                        "model_name": group["model_name"],
                        "checksum": address(group["checksum"]),
                        "stream": POSE_STREAM if pose else ANIMATION_STREAM,
                        "entities": len(pose or animation),
                    }
                )
            continue
        counts: Counter[int] = Counter()
        for source in pose:
            for target in animation:
                counts[(target - source) & POINTER_MASK] += 1
        ranked = sorted(counts.items(), key=lambda item: (-item[1], item[0]))
        constant = ranked[0][0] if len(ranked) == 1 else None
        shared_rows.append(
            {
                "model_name": group["model_name"],
                "checksum": address(group["checksum"]),
                "bone_count": group["bone_count"],
                "pose_entities": len(pose),
                "animation_entities": len(animation),
                "constant_delta": None if constant is None else address(constant),
                "distinct_differences": len(counts),
                "differences": [
                    {
                        "delta": address(delta),
                        "delta_signed": signed(delta),
                        "pairs": pairs,
                        "candidate": delta in candidates,
                    }
                    for delta, pairs in ranked[:MAX_REPORTED_DIFFERENCES]
                ],
                "differences_truncated": len(ranked) > MAX_REPORTED_DIFFERENCES,
            }
        )
    return {
        "shared_models": shared_rows,
        "shared_models_truncated": False,
        "models_in_one_stream_only": single_stream,
    }


def pointer_reuse(connection: sqlite3.Connection) -> dict[str, Any]:
    """A raw-address join is invalid wherever one address served two models."""
    rows = [
        {
            "stream": stream,
            "client_entity": address(entity),
            "distinct_checksums": checksums,
        }
        for stream, entity, checksums in connection.execute(
            """
            SELECT stream_name, client_entity, count(DISTINCT checksum)
            FROM records GROUP BY stream_name, client_entity
            HAVING count(DISTINCT checksum) > 1
            ORDER BY 3 DESC
            """
        )
    ]
    return {
        "reused_entities": len(rows),
        "entities": rows[:MAX_REPORTED_REUSE],
        "entities_truncated": len(rows) > MAX_REPORTED_REUSE,
    }


def lifetimes(connection: sqlite3.Connection) -> dict[str, dict[int, tuple[int, int]]]:
    spans: dict[str, dict[int, tuple[int, int]]] = {}
    for stream, entity, first, last in connection.execute(
        "SELECT stream_name, client_entity, min(qpc), max(qpc) FROM records "
        "GROUP BY stream_name, client_entity"
    ):
        spans.setdefault(stream, {})[entity] = (first, last)
    return spans


def timing_diagnostic(
    connection: sqlite3.Connection,
    groups: dict[int, dict[str, Any]],
    delta: int | None,
    frequency: int,
) -> dict[str, Any]:
    note = (
        "Diagnostic only. The delta above is derived from model identity; no "
        "pairing in this report comes from a timestamp."
    )
    if delta is None:
        return {"authoritative": False, "note": note, "pairs": 0}
    spans = lifetimes(connection)
    pose_spans = spans.get(POSE_STREAM, {})
    animation_spans = spans.get(ANIMATION_STREAM, {})
    overlapping = 0
    disjoint = 0
    worst = 0
    for group in groups.values():
        for source in group[POSE_STREAM]:
            target = (source + delta) & POINTER_MASK
            if target not in group[ANIMATION_STREAM]:
                continue
            first, last = pose_spans[source]
            other_first, other_last = animation_spans[target]
            gap = max(other_first - last, first - other_last, 0)
            if gap:
                disjoint += 1
                worst = max(worst, gap)
            else:
                overlapping += 1
    return {
        "authoritative": False,
        "note": note,
        "pairs": overlapping + disjoint,
        "overlapping": overlapping,
        "disjoint": disjoint,
        "worst_gap_seconds": round(worst / frequency, 3),
    }


def decide(search: dict[str, Any], reuse: dict[str, Any]) -> dict[str, Any]:
    candidates = search["candidates"]
    shared = search["shared_models"]
    leading = search["leading"][0] if search["leading"] else None
    verdict = {
        "one_pointer_space": False,
        "constant_delta": None,
        "constant_delta_signed": None,
        "ambiguous": len(candidates) > 1,
        "join_owner": "CAP2.1-generation",
        "derivation": "model identity only; no timestamp correlation",
    }
    if not shared:
        verdict["statement"] = (
            "No model checksum appears in both streams, so the two pointer "
            "sets cannot be related by model identity at all."
        )
        return verdict
    if not candidates:
        best = (
            "none"
            if leading is None
            else (
                f"the best-supported difference {leading['delta']} holds for "
                f"{leading['models_supporting']} of {shared} shared models"
            )
        )
        verdict["statement"] = (
            "No difference holds across every shared model, so the draw and "
            f"skeletal streams are not one pointer space ({best}). CAP2.1's "
            "pose-build generation is the only join."
        )
        return verdict
    if len(candidates) > 1:
        verdict["statement"] = (
            f"{len(candidates)} differences survive every shared model, so no "
            "single delta is proven. This is ambiguity, not a join; CAP2.1's "
            "pose-build generation remains the only join."
        )
        return verdict

    candidate = candidates[0]
    verdict["constant_delta"] = candidate["delta"]
    verdict["constant_delta_signed"] = candidate["delta_signed"]
    if shared < MINIMUM_SHARED_MODELS:
        verdict["statement"] = (
            f"Only {shared} model appears in both streams, which is too little "
            f"evidence to call {candidate['delta']} a pointer-space rule."
        )
        return verdict
    if not candidate["covers_all"]:
        verdict["statement"] = (
            f"{candidate['delta']} resolves "
            f"{candidate['resolved_animation_entities']} of "
            f"{candidate['animation_entities']} skeletal entities to a drawn "
            "entity, so it is a partial relationship rather than one pointer "
            "space. CAP2.1's pose-build generation is the only join."
        )
        return verdict
    verdict["one_pointer_space"] = True
    verdict["join_owner"] = "CAP1.3-delta"
    verdict["statement"] = (
        "The draw and skeletal streams are one pointer space: the draw "
        "stream's render-info entity field plus "
        f"{candidate['delta_signed']:+d} is the skeletal instance pointer. "
        f"It is the only difference holding across all {shared} shared "
        f"models, and it resolves every one of the "
        f"{candidate['animation_entities']} skeletal entities to a drawn "
        f"entity. {candidate['pose_with_counterpart']} of "
        f"{candidate['pose_entities']} drawn entities have a skeletal "
        "counterpart; the rest are drawn without a skeletal evaluation."
    )
    if candidate["bijective_models"] != shared:
        verdict["statement"] += (
            f" {shared - candidate['bijective_models']} model group is "
            "lopsided because a skeletal actor was drawn under another model, "
            "which is model attribution rather than a pointer-space failure."
        )
    if reuse["reused_entities"]:
        verdict["statement"] += (
            f" {reuse['reused_entities']} entity addresses served more than "
            "one model in this run, so the join is only valid inside an "
            "address's lifetime."
        )
    return verdict


def verify(session: Path) -> dict[str, Any]:
    connection = open_database(session)
    try:
        metadata = {
            key: json.loads(value)
            for key, value in connection.execute(
                "SELECT key, value FROM capture_metadata"
            )
        }
        headers = stream_headers(connection)
        frequency = next(iter(headers.values()))["qpc_frequency"]
        bases = module_bases(headers)

        entities = entity_sets(connection)
        pose_entities = entities.get(POSE_STREAM, set())
        animation_entities = entities.get(ANIMATION_STREAM, set())
        groups = model_groups(connection)
        search = delta_search(groups, pose_entities, animation_entities)
        reuse = pointer_reuse(connection)
        verdict = decide(search, reuse)
        candidates = {
            int(entry["delta"], 16) for entry in search["candidates"]
        }
        accepted = (
            int(verdict["constant_delta"], 16)
            if verdict["one_pointer_space"]
            else None
        )

        return {
            "session": session.name,
            "session_path": str(session),
            "database": str(session / DATABASE_NAME),
            "identity": {
                "map": metadata.get("map"),
                "created_utc": metadata.get("created_utc"),
                "tool_git": metadata.get("tool_git"),
                "modules": {
                    name: sha
                    for name, sha in connection.execute(
                        "SELECT name, sha256 FROM modules"
                    )
                },
                "module_bases": {
                    name: address(value) for name, value in bases.items()
                },
            },
            "space": {
                "pose": geometry(pose_entities, bases),
                "animation": geometry(animation_entities, bases),
                # Kept whole: the cross-run comparison needs the addresses
                # themselves to tell a fixed offset from a repeated layout.
                "animation_values": sorted(
                    address(value) for value in animation_entities
                ),
                "shared_values": len(pose_entities & animation_entities),
                "shared_examples": [
                    address(value)
                    for value in sorted(pose_entities & animation_entities)[:8]
                ],
            },
            "cross_field": cross_field_sweep(
                connection, pose_entities, animation_entities
            ),
            "delta": search,
            "per_model": distribution(groups, candidates),
            "pointer_reuse": reuse,
            "timing": timing_diagnostic(connection, groups, accepted, frequency),
            "verdict": verdict,
        }
    finally:
        connection.close()


def compare(
    reports: list[dict[str, Any]], entities: list[set[int]]
) -> dict[str, Any]:
    rows = [
        {
            "session": report["session"],
            "constant_delta": report["verdict"]["constant_delta"],
            "one_pointer_space": report["verdict"]["one_pointer_space"],
            "join_owner": report["verdict"]["join_owner"],
            "shared_models": report["delta"]["shared_models"],
            "carrying_fields": report["cross_field"]["carrying_fields"],
        }
        for report in reports
    ]
    # A delta that repeats while no address repeats is a struct offset; a delta
    # that only repeats because the heap did is nothing.
    shared_addresses = len(set.intersection(*entities)) if entities else 0
    deltas = {row["constant_delta"] for row in rows}
    stable = next(iter(deltas)) if len(deltas) == 1 else None
    positive = all(row["one_pointer_space"] for row in rows)
    if stable is not None and positive:
        statement = (
            f"Every run resolves to the same difference {stable} while the "
            f"runs share {shared_addresses} skeletal instance addresses, so "
            "the difference is a fixed offset in the object rather than a "
            "coincidence of one heap layout."
        )
    elif positive:
        statement = (
            "Every run is internally consistent but the difference is not the "
            f"same integer across runs ({sorted(map(str, deltas))}), so it is "
            "a per-run layout coincidence and not a join."
        )
    else:
        statement = (
            "At least one run does not resolve to a single pointer space, so "
            "CAP2.1's pose-build generation is the only join."
        )
    return {
        "sessions": [report["session"] for report in reports],
        "runs": rows,
        "stable_delta": stable if positive else None,
        "skeletal_addresses_shared_between_runs": shared_addresses,
        "statement": statement,
    }


def summarize(report: dict[str, Any]) -> str:
    space = report["space"]
    search = report["delta"]
    verdict = report["verdict"]
    carrying = report["cross_field"]["carrying_fields"]
    lines = [f"{report['session']}"]
    lines.append(
        f"  space      pose {space['pose']['distinct']} / "
        f"animation {space['animation']['distinct']} distinct; "
        f"shared values {space['shared_values']}"
    )
    lines.append(
        "  fields     "
        + (
            f"carried by {', '.join(carrying)}"
            if carrying
            else "no draw record field carries a skeletal instance pointer"
        )
    )
    lines.append(
        f"  models     {search['shared_models']} shared, "
        f"{search['pose_only_models']} pose-only, "
        f"{search['animation_only_models']} animation-only"
    )
    if search["candidates"]:
        best = search["candidates"][0]
        lines.append(
            f"  delta      candidates={len(search['candidates'])}  "
            f"{best['delta']} ({best['delta_signed']:+d})  resolves "
            f"{best['resolved_animation_entities']}/"
            f"{best['animation_entities']} skeletal entities, bijective in "
            f"{best['bijective_models']}/{best['shared_models']} models"
        )
    else:
        leading = search["leading"][0] if search["leading"] else None
        lines.append(
            "  delta      candidates=0  "
            + (
                "no difference observed"
                if leading is None
                else f"best {leading['delta']} in "
                f"{leading['models_supporting']}/{leading['shared_models']} models"
            )
        )
    lines.append(
        f"  verdict    {'ONE POINTER SPACE' if verdict['one_pointer_space'] else 'SEPARATE'}"
        f" — join owner {verdict['join_owner']}"
    )
    lines.append(f"             {verdict['statement']}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--report", type=Path)
    parser.add_argument(
        "--no-session-reports",
        action="store_true",
        help="Do not write entity-join.json beside each database.",
    )
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        report = verify(session)
        reports.append(report)
        if not args.no_session_reports:
            (session / "entity-join.json").write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8"
            )
        print(summarize(report), flush=True)

    entities = [
        {int(value, 16) for value in report["space"]["animation_values"]}
        for report in reports
    ]
    combined = {
        "sessions": [report["session"] for report in reports],
        "reports": reports,
        "comparison": compare(reports, entities) if len(reports) > 1 else None,
    }
    if combined["comparison"]:
        print(combined["comparison"]["statement"], flush=True)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(combined, indent=2) + "\n", encoding="utf-8"
        )
        print(args.report.resolve(), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
