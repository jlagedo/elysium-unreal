"""Name every activity a retail capture observed, by joining it to model metadata.

Usage:
    uv run elysium research build_clip_table <session>

`verify_gameplay_actions` reports which activity number resolved to which engine
sequence index, and nothing more -- the numbers are the server's, and the server
never writes their names down. The models do: `szactivitynameindex` on each
sequence descriptor carries the activity literal (`ACT_IDLE`, `ACT_RUN_GLOCK`),
beside the `actweight` that governs the weighted draw, the blend grid, the
authored transition duration and the cycle's ground speed.

So the capture's `(model, activity number) -> sequence index` becomes a named
clip table by indexing each model's include tree the way the engine does. Which
ordering that is gets *decided*, not assumed: both candidates are scored by how
many activity groups they label with a single activity name, and the winner is
reported beside the table so a bad join is visible rather than silent.

The table is game-derived, so it is written beside the session under
ELYSIUM_WORK_ROOT and never into the checkout.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import sqlite3
import struct
from typing import Any, Callable, Iterable

from elysium_pipeline.formats import mdl_skel as S

from research.tooling.capture.calibrate_theatre_capture import (
    DATABASE_NAME,
    resolve_session,
)


TABLE_NAME = "clip-table.json"
SELECTION_KINDS = ("SWGT", "SHVY")
#: StudioSeqDesc stride, and the field offsets this join reads from one.
SEQDESC_STRIDE = 764
SEQDESC_LABEL = 0
SEQDESC_ACTIVITY_NAME = 4
SEQDESC_ACTWEIGHT = 16
SEQDESC_FADE = 612
#: Header fields naming the sequence and animation arrays.
NUM_LOCAL_SEQ = 272
LOCAL_SEQ_INDEX = 276
NUM_LOCAL_ANIM = 264
LOCAL_ANIM_INDEX = 268
ANIMDESC_STRIDE = 72


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _f32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def _cstr(data: bytes, base: int, field: int) -> str:
    start = base + _i32(data, base + field)
    return data[start:data.index(b"\0", start)].decode("ascii", "replace")


def flatten_tree(
    load: Callable[[str], bytes | None], model: str, *, dedup: bool
) -> list[tuple[str, bytes, int]]:
    """One model's include tree as a flat sequence array -> [(key, bytes, i), ...].

    The two candidates differ only in whether a model reachable by several
    include paths contributes its descriptors once or once per reference.
    `dedup=True` matches `mdl_skel.resolve_tree`; `dedup=False` re-walks it,
    guarding only against a true cycle. Which one the engine's virtual model
    builds is settled by `score_orderings`, not by this function.
    """
    models: list[tuple[str, bytes]] = []
    seen: set[str] = set()

    def visit(key: str, stack: frozenset[str]) -> None:
        lowered = key.lower()
        if dedup:
            if lowered in seen:
                return
            seen.add(lowered)
        elif lowered in stack:
            return
        data = load(key)
        if data is None:
            return
        models.append((key, data))
        for include in S.read_includes(data):
            visit(include, stack | {lowered})

    visit(model, frozenset())
    return [
        (key, data, index)
        for key, data in models
        for index in range(_i32(data, NUM_LOCAL_SEQ))
    ]


def activity_name_at(flat: list[tuple[str, bytes, int]], sequence: int) -> str | None:
    """The activity literal on one flat sequence index, or None if it overruns."""
    if not 0 <= sequence < len(flat):
        return None
    _, data, index = flat[sequence]
    base = _i32(data, LOCAL_SEQ_INDEX) + index * SEQDESC_STRIDE
    return _cstr(data, base, SEQDESC_ACTIVITY_NAME)


def score_orderings(
    observed: dict[tuple[str, int], Counter],
    flats: dict[str, dict[str, list[tuple[str, bytes, int]]]],
    name_at: Callable[[list[tuple[str, bytes, int]], int], str | None] = activity_name_at,
) -> dict[str, dict[str, int]]:
    """How well each candidate ordering explains the capture.

    A correct ordering labels every sequence the engine chose for one activity
    number with the same activity name -- the number *is* that name's enum. A
    wrong one scatters unrelated clips across a group, and runs off the end of
    the array. Both are counted, and both are reported.
    """
    scores: dict[str, dict[str, int]] = {}
    for mode in ("dedup", "nodedup"):
        groups = contradictions = overruns = 0
        for (model, _activity), sequences in observed.items():
            flat = flats.get(model, {}).get(mode)
            if flat is None:
                continue
            groups += 1
            names = set()
            for sequence in sequences:
                name = name_at(flat, sequence)
                if name is None:
                    overruns += 1
                else:
                    names.add(name)
            if len(names) > 1:
                contradictions += 1
        scores[mode] = {
            "groups": groups,
            "contradictions": contradictions,
            "overruns": overruns,
        }
    return scores


def best_ordering(scores: dict[str, dict[str, int]]) -> str:
    return min(
        scores,
        key=lambda mode: (scores[mode]["contradictions"], scores[mode]["overruns"]),
    )


def describe_clip(flat: list[tuple[str, bytes, int]], sequence: int) -> dict[str, Any]:
    """Everything a remake resolver needs about one chosen sequence."""
    key, data, index = flat[sequence]
    base = _i32(data, LOCAL_SEQ_INDEX) + index * SEQDESC_STRIDE
    grid = S.read_grid(data, base)
    cell = grid.cells[0].anim
    animation = _i32(data, LOCAL_ANIM_INDEX) + cell * ANIMDESC_STRIDE
    frames, fps = _i32(data, animation + 12), _f32(data, animation + 4)
    movement = (
        S.movement_summary(data, animation, frames, fps)
        if 0 <= cell < _i32(data, NUM_LOCAL_ANIM)
        else None
    )
    return {
        "sequence_index": sequence,
        "label": _cstr(data, base, SEQDESC_LABEL),
        "activity_name": _cstr(data, base, SEQDESC_ACTIVITY_NAME),
        "actweight": _i32(data, base + SEQDESC_ACTWEIGHT),
        "bank": key,
        "blend_cells": grid.numblends,
        "blend_groupsize": list(grid.groupsize),
        "blend_parameter": grid.paramindex,
        "blend_range": [grid.paramstart, grid.paramend],
        "frames": frames,
        "fps": round(fps, 4),
        "fade_seconds": round(_f32(data, base + SEQDESC_FADE), 4),
        "ground_speed_cm_s": (
            round(movement.ground_speed_cm_s, 3) if movement else None
        ),
    }


def read_observations(
    database: Path,
) -> tuple[dict[tuple[str, int], Counter], dict[int, str]]:
    """(model, activity) -> {sequence index: draws}, plus each entity's model."""
    uri = database.as_uri() + "?mode=ro"
    connection = sqlite3.connect(uri, uri=True)
    try:
        models = {
            int(index): str(model)
            for index, model in connection.execute(
                """SELECT a.entity_index, a.model_name FROM actor_observations a
                   JOIN (SELECT entity_index, max(qpc) q FROM actor_observations
                         WHERE model_name != '' GROUP BY 1) latest
                     ON latest.entity_index = a.entity_index
                        AND latest.q = a.qpc
                   WHERE a.model_name != ''"""
            )
        }
        observed: dict[tuple[str, int], Counter] = defaultdict(Counter)
        for index, activity, sequence, draws in connection.execute(
            f"""SELECT entity_index, input_value, selected_sequence, count(*)
                FROM gameplay_action_events
                WHERE kind IN {SELECTION_KINDS} AND entity_index IS NOT NULL
                  AND selected_sequence >= 0
                GROUP BY 1, 2, 3"""
        ):
            model = models.get(int(index))
            if model:
                observed[(model, int(activity))][int(sequence)] += int(draws)
    finally:
        connection.close()
    return observed, models


def model_key(model: str) -> str:
    """A capture's client model path as the install index spells it."""
    key = "models/" + model.replace("\\", "/").lstrip("/")
    return key if key.lower().endswith(".mdl") else key + ".mdl"


def resolve_model_key(
    index: Any, model: str, load: Callable[[str], bytes | None]
) -> tuple[str | None, bool]:
    """(key, was_truncated). Recovers a model name the actor stream cut short.

    ACTOR observation records carry a fixed-width model name, so a path longer
    than the field arrives without its tail -- `..._Ref.md` for `..._Ref.mdl`,
    or a whole missing extension. Such a name is still a unique prefix of one
    installed model in every case this has been seen, so it is resolved by
    prefix and reported as recovered rather than dropped. An ambiguous prefix
    resolves to nothing, because guessing which clip table to attribute to an
    NPC is worse than leaving it out.
    """
    key = model_key(model)
    if load(key) is not None:
        return key, False
    prefix = ("models/" + model.replace("\\", "/").lstrip("/")).lower()
    matches = sorted(
        candidate
        for candidate in index
        if candidate.lower().startswith(prefix)
        and candidate.lower().endswith(".mdl")
    )
    if len(matches) == 1 and load(matches[0]) is not None:
        return matches[0], True
    return None, False


def build(session_or_database: Path) -> dict[str, Any]:
    if session_or_database.is_file():
        session, database = session_or_database.parent, session_or_database
    else:
        session = resolve_session(str(session_or_database))
        database = session / DATABASE_NAME
    if not database.is_file():
        raise FileNotFoundError(database)

    # Imported here rather than at module scope: `install` resolves the VtMB
    # root the moment it loads, so importing it eagerly would make this module
    # unimportable -- and its pure join logic untestable -- without an install.
    from elysium_pipeline.formats import install

    observed, _models = read_observations(database)
    index = install.build_index()

    def load(key: str) -> bytes | None:
        stem = key[:-4] if key.lower().endswith(".mdl") else key
        return install.read(index, stem + ".mdl") or None

    flats: dict[str, dict[str, list[tuple[str, bytes, int]]]] = {}
    unreadable = []
    recovered = {}
    for model in sorted({model for model, _ in observed}):
        key, truncated = resolve_model_key(index, model, load)
        if key is None:
            unreadable.append(model)
            continue
        if truncated:
            recovered[model] = key
        flats[model] = {
            "dedup": flatten_tree(load, key, dedup=True),
            "nodedup": flatten_tree(load, key, dedup=False),
        }

    scores = score_orderings(observed, flats)
    ordering = best_ordering(scores)

    table: dict[str, dict[str, list[dict[str, Any]]]] = {}
    unresolved = 0
    for (model, activity), sequences in sorted(observed.items()):
        flat = flats.get(model, {}).get(ordering)
        if flat is None:
            continue
        clips = []
        for sequence, draws in sorted(sequences.items(), key=lambda kv: -kv[1]):
            if not 0 <= sequence < len(flat):
                unresolved += 1
                continue
            clips.append({**describe_clip(flat, sequence), "observed_draws": draws})
        if clips:
            table.setdefault(model, {})[str(activity)] = clips

    named: dict[int, set[str]] = defaultdict(set)
    for model, activities in table.items():
        for activity, clips in activities.items():
            for clip in clips:
                if clip["activity_name"]:
                    named[int(activity)].add(clip["activity_name"])

    report = {
        "session": str(session),
        "database": str(database),
        "orderings": scores,
        "ordering": ordering,
        "models_unreadable": unreadable,
        "models_recovered_from_truncated_name": recovered,
        "sequences_unresolved": unresolved,
        "activity_names": {
            str(activity): sorted(names) for activity, names in sorted(named.items())
        },
        "models": table,
    }
    (session / TABLE_NAME).write_text(
        json.dumps(report, indent=1) + "\n", encoding="utf-8"
    )
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    arguments = parser.parse_args()
    report = build(arguments.session)
    print(
        json.dumps(
            {
                key: value
                for key, value in report.items()
                if key != "models"
            },
            indent=2,
        )
    )
    print(f"\nwrote {Path(report['session']) / TABLE_NAME}")
    return 0 if not report["models_unreadable"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
