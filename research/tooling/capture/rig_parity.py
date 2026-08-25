"""Check this repository's rig answers against a retail rig-resolution capture.

A `life_rig_resolution` / `life_rig_chaos` session (`analyze_rig_resolution`,
`capture_rig_remap`) records what retail did: the flat sequence-number space each
body built at load, and which bank and clip every committed selection resolved
to. The export answers the same two questions from the install alone -- the
include-DAG walk numbers the space, and `npc/clips/<stem>.json` names one owner
per label -- so the two can be diffed row for row. This tool does that and
writes the result beside the session as `rig_oracle.json`:

* `numbering`: for every body the session read, the offline flat space
  (`build_clip_table.flatten_tree`, both orderings) against the live
  `sequence_map.csv` -- owner, local index and label per global number.
* `ownership`: for every (body, label) retail committed, the owner stem the
  export's clip map names against the bank retail resolved through.
* `oracle`: the witnessed selections rewritten in the export's own stem
  namespace, so a runtime test can replay them without reading the install.

Exit status is 1 on any divergence. Nothing here is committed: the capture and
the report stay under ELYSIUM_WORK_ROOT.

Usage:
    uv run elysium research rig_parity
    uv run elysium research rig_parity --session <capture directory>
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path
import sys
from typing import Any, Callable, Iterable

from elysium_pipeline.formats import mdl_skel

from research.tooling.capture.build_clip_table import flatten_tree

REPORT_NAME = "rig_oracle.json"
RECIPES = ("life_rig_resolution", "life_rig_chaos")
ORDERINGS = ("dedup", "nodedup")
#: How many divergent rows a section keeps verbatim; the counts are complete.
EXAMPLES = 25

Loader = Callable[[str], "bytes | None"]


def model_key(name: str) -> str:
    """A captured `Name`@12 or an export `model` as one `models/`-rooted key."""
    key = name.replace("\\", "/").lower().strip().lstrip("/")
    return key if key.startswith("models/") else "models/" + key


# --- the session -----------------------------------------------------------------------------


def latest_session() -> Path:
    from elysium_pipeline.paths import research_root

    root = research_root() / "frida"
    candidates = sorted(
        (
            path
            for recipe in RECIPES
            for path in root.glob(f"*-{recipe}")
            if (path / "sequence_map.csv").is_file()
        ),
        key=lambda path: path.name,
    )
    if not candidates:
        raise FileNotFoundError(
            f"no {' or '.join(RECIPES)} session below {root} carries a "
            "sequence_map.csv; run capture_rig_remap while the game is open"
        )
    return candidates[-1]


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


# --- numbering: the offline flat space against the live one ----------------------------------


def offline_numbering(load: Loader, body: str) -> dict[str, list[dict[str, Any]]]:
    """Both candidate flat spaces for one body -> ordering -> [row per global index].

    Each row names the owning model, its local index and the label and activity
    literal on that descriptor, read positionally the way the engine numbers them.
    """
    labels: dict[str, list[str]] = {}
    activities: dict[str, list[str]] = {}
    out: dict[str, list[dict[str, Any]]] = {}
    for ordering in ORDERINGS:
        rows = []
        for key, data, index in flatten_tree(load, body, dedup=(ordering == "dedup")):
            if key not in labels:
                labels[key] = mdl_skel.local_sequence_labels(data)
                activities[key] = mdl_skel.local_sequence_activities(data)
            rows.append(
                {
                    "global_index": len(rows),
                    "owner_model": model_key(key),
                    "owner_index": index,
                    "label": labels[key][index],
                    "activity_name": activities[key][index],
                }
            )
        out[ordering] = rows
    return out


def compare_numbering(
    live: Iterable[dict[str, Any]], offline: list[dict[str, Any]]
) -> dict[str, Any]:
    """One body's live `sequence_map.csv` rows against one offline flat space.

    A live row whose owner the capture could not read (`resolved` false) still
    carries the owner model and index the engine's group ranges named, so those
    two are compared on every row; the label only where the capture read one.
    """
    live_rows = sorted(live, key=lambda row: int(row["global_index"]))
    compared = 0
    owner_mismatches: list[dict[str, Any]] = []
    label_mismatches: list[dict[str, Any]] = []
    for row in live_rows:
        index = int(row["global_index"])
        if index >= len(offline):
            owner_mismatches.append(
                {"global_index": index, "live": row.get("owner_model"), "offline": None}
            )
            continue
        ours = offline[index]
        compared += 1
        live_owner = model_key(row["owner_model"]) if row.get("owner_model") else ""
        if live_owner != ours["owner_model"] or int(row["owner_index"]) != ours["owner_index"]:
            owner_mismatches.append(
                {
                    "global_index": index,
                    "live": f"{live_owner}#{row['owner_index']}",
                    "offline": f"{ours['owner_model']}#{ours['owner_index']}",
                }
            )
            continue
        if row.get("resolved") == "True" and row.get("label") != ours["label"]:
            label_mismatches.append(
                {"global_index": index, "live": row.get("label"), "offline": ours["label"]}
            )
    return {
        "live_total": len(live_rows),
        "offline_total": len(offline),
        "compared": compared,
        "owner_mismatches": len(owner_mismatches),
        "label_mismatches": len(label_mismatches),
        "examples": (owner_mismatches + label_mismatches)[:EXAMPLES],
    }


def best_numbering(
    live: list[dict[str, Any]], offline: dict[str, list[dict[str, Any]]]
) -> tuple[str, dict[str, Any]]:
    """The ordering that explains the live space best, and its comparison."""
    scored = {ordering: compare_numbering(live, rows) for ordering, rows in offline.items()}
    ordering = min(
        scored,
        key=lambda name: (
            scored[name]["owner_mismatches"] + scored[name]["label_mismatches"],
            abs(scored[name]["offline_total"] - scored[name]["live_total"]),
            ORDERINGS.index(name),
        ),
    )
    return ordering, scored[ordering]


# --- ownership: the export's owner per label against retail's ---------------------------------


def witnessed_owners(
    resolution: Iterable[dict[str, str]], ownership: Iterable[dict[str, str]]
) -> dict[str, dict[str, dict[str, Any]]]:
    """body key -> lowercased label -> what retail resolved that label to.

    Every selection row that resolved (`resolution.csv`) and every ownership
    chain (`ownership.csv`) witnesses one (body, label) -> owner. A label seen
    under two owners on one body is kept as a conflict rather than collapsed.
    """
    seen: dict[str, dict[str, dict[str, Any]]] = defaultdict(dict)

    def witness(body: str, label: str, owner: str, activity: str, target: str,
                requested: str, sequence: str) -> None:
        if not (body and label and owner):
            return
        entry = seen[model_key(body)].setdefault(
            label.lower(),
            {
                "label": label,
                "owners": Counter(),
                "activity_names": Counter(),
                "targets": Counter(),
                "activity_ids": Counter(),
                "sequences": Counter(),
            },
        )
        entry["owners"][model_key(owner)] += 1
        if activity:
            entry["activity_names"][activity] += 1
        entry["targets"][target] += 1
        if requested:
            entry["activity_ids"][requested] += 1
        if sequence:
            entry["sequences"][sequence] += 1

    for row in resolution:
        witness(
            row.get("body_model", ""), row.get("resolved_label", ""),
            row.get("resolved_owner", ""), row.get("resolved_activity_name", ""),
            row.get("target", ""),
            row.get("requested", "") if row.get("result_kind") == "sequence" else "",
            row.get("result", "") if row.get("result_kind") == "sequence" else "",
        )
    for row in ownership:
        witness(
            row.get("requested_model", ""), row.get("label", ""), row.get("owner_model", ""),
            row.get("activity_name", ""), "client.resolve_sequence_owner", "",
            row.get("requested_index", ""),
        )
    return seen


def first_occurrence_owners(flat: Iterable[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    """lowercased label -> the owner of its first global number, for one flat space.

    Read off the offline numbering rather than the live table: a live row whose
    bank the session never loaded carries no label, and skipping it would move a
    label's first occurrence to the next bank that repeats it.
    """
    owners: dict[str, dict[str, Any]] = {}
    for row in flat:
        label = row.get("label")
        if not label:
            continue
        owners.setdefault(
            label.lower(),
            {"label": label, "owner_model": row["owner_model"],
             "global_index": row["global_index"]},
        )
    return owners


def reachable_owners(flat: Iterable[dict[str, Any]]) -> set[str]:
    """Every owner model one flat space reaches."""
    return {row["owner_model"] for row in flat}


def admissible_witnesses(
    witnessed: dict[str, dict[str, Any]], reachable: set[str]
) -> tuple[dict[str, dict[str, Any]], dict[str, dict[str, Any]]]:
    """Split one body's witnesses into those its include tree can reach and those it cannot.

    A body resolves a number only through the banks its own tree includes, so a
    witness naming any other bank is a capture artifact -- a chain assembled from
    another entity's links under sampling -- and is reported rather than diffed.
    """
    admitted: dict[str, dict[str, Any]] = {}
    rejected: dict[str, dict[str, Any]] = {}
    for lowered, entry in witnessed.items():
        owners = {owner: hits for owner, hits in entry["owners"].items() if owner in reachable}
        if not owners:
            rejected[lowered] = entry
            continue
        if len(owners) != len(entry["owners"]):
            rejected[lowered] = {**entry, "owners": Counter(
                {owner: hits for owner, hits in entry["owners"].items() if owner not in reachable}
            )}
        admitted[lowered] = {**entry, "owners": Counter(owners)}
    return admitted, rejected


def label_collisions(flat: Iterable[dict[str, Any]]) -> dict[str, Any]:
    """Labels one flat space carries more than once, and whether the copies share an activity.

    Retail draws among every descriptor a number space carries; an export keyed by
    label can keep one per label. A collision whose copies name different
    activities is a clip the label-keyed map cannot reach at all; one whose copies
    share an activity is a candidate the draw cannot see.
    """
    copies: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in flat:
        if row.get("label"):
            copies[row["label"].lower()].append(row)
    same_activity = []
    different_activity = []
    for lowered, rows in sorted(copies.items()):
        owners = {row["owner_model"] for row in rows}
        if len(owners) < 2:
            continue
        activities = {row["activity_name"] for row in rows}
        record = {
            "label": rows[0]["label"],
            "owners": sorted(owners),
            "activities": sorted(activities),
        }
        (different_activity if len(activities) > 1 else same_activity).append(record)
    return {
        "labels_repeated_across_banks": len(same_activity) + len(different_activity),
        "same_activity": len(same_activity),
        "different_activity": len(different_activity),
        "examples": {
            "same_activity": same_activity[:EXAMPLES],
            "different_activity": different_activity[:EXAMPLES],
        },
    }


def load_export_index(export_root: Path) -> dict[str, Any]:
    path = export_root / "npc" / "npc_index.json"
    if not path.is_file():
        raise FileNotFoundError(path)
    return json.loads(path.read_text(encoding="utf-8"))


def stems_by_model(index: dict[str, Any]) -> dict[str, str]:
    """export model key -> stem, over bodies and banks alike."""
    stems: dict[str, str] = {}
    for section in ("npcs", "banks"):
        for stem, record in (index.get(section) or {}).items():
            model = record.get("model")
            if model:
                stems[model_key(model)] = stem
    return stems


def export_clip_owners(export_root: Path, stem: str) -> dict[str, list[dict[str, str]]]:
    """lowercased label -> [{label, owner stem, activity}, ...] from `npc/clips/<stem>.json`.

    A label carries one row per declaring owner, in include-tree order; a slice written before
    the copies were kept stores its single row bare, and is told apart by its first element
    being a number rather than an array.
    """
    path = export_root / "npc" / "clips" / f"{stem}.json"
    if not path.is_file():
        raise FileNotFoundError(path)
    data = json.loads(path.read_text(encoding="utf-8"))
    owners = data["owners"]
    activities = data["activities"]
    numbers = data.get("seq", {})
    out: dict[str, list[dict[str, str]]] = {}
    for label, value in data["clips"].items():
        rows = value if value and isinstance(value[0], list) else [value]
        stated = numbers.get(label, [])
        out[label.lower()] = [
            {
                "label": label,
                "owner_stem": owners[row[0]],
                "activity_name": activities[row[1]],
                # The global sequence number this row answers at, positionally parallel to the
                # rows; a slice written before the numbering existed states none.
                "seq": stated[index] if index < len(stated) else None,
            }
            for index, row in enumerate(rows)
        ]
    return out


def compare_sequence_numbers(
    exported: dict[str, list[dict[str, Any]]],
    live: Iterable[dict[str, str]],
    stem_of: dict[str, str],
) -> dict[str, Any]:
    """Every exported row's global sequence number against the number the running game gave it.

    `sequence_map.csv` is the engine's own flat space read out of the live process: for each
    global index, the bank that owns it and the index within that bank. The export derives the
    same numbering offline from the include tree, so every row it numbers can be looked up in
    the live table and must name the same owner and the same label. A row the export could not
    number, or one the live table's body never loaded, is counted rather than compared.
    """
    by_index = {int(row["global_index"]): row for row in live}
    checked = 0
    unnumbered = 0
    absent = 0
    owner_mismatches: list[dict[str, Any]] = []
    label_mismatches: list[dict[str, Any]] = []
    for lowered, rows in sorted(exported.items()):
        for row in rows:
            number = row.get("seq")
            if number is None:
                unnumbered += 1
                continue
            answer = by_index.get(int(number))
            if answer is None:
                absent += 1
                continue
            checked += 1
            live_stem = stem_of.get(model_key(answer["owner_model"]) if answer["owner_model"]
                                    else "")
            if live_stem is not None and live_stem != row["owner_stem"]:
                owner_mismatches.append(
                    {"label": row["label"], "seq": number,
                     "live": live_stem, "export": row["owner_stem"]}
                )
                continue
            if answer.get("label") and answer["label"].lower() != lowered:
                label_mismatches.append(
                    {"seq": number, "live": answer["label"], "export": row["label"]}
                )
    return {
        "rows_checked": checked,
        "rows_unnumbered": unnumbered,
        "numbers_absent_from_live_table": absent,
        "owner_mismatches": len(owner_mismatches),
        "label_mismatches": len(label_mismatches),
        "examples": (owner_mismatches + label_mismatches)[:EXAMPLES],
    }


def compare_ownership(
    witnessed: dict[str, dict[str, Any]],
    exported: dict[str, dict[str, str]],
    stem_of: dict[str, str],
) -> dict[str, Any]:
    """One body's witnessed (label -> owner) against its exported clip map."""
    absent: list[dict[str, Any]] = []
    conflicts: list[dict[str, Any]] = []
    owner_mismatches: list[dict[str, Any]] = []
    activity_mismatches: list[dict[str, Any]] = []
    unmapped_owners: Counter = Counter()
    agreed = 0
    for lowered, entry in sorted(witnessed.items()):
        retail_owners = sorted(entry["owners"])
        if len(retail_owners) > 1:
            conflicts.append({"label": entry["label"], "owners": retail_owners})
            continue
        retail_owner = retail_owners[0]
        retail_stem = stem_of.get(retail_owner)
        if retail_stem is None:
            unmapped_owners[retail_owner] += 1
            continue
        rows = exported.get(lowered)
        if not rows:
            absent.append({"label": entry["label"], "retail_owner": retail_stem})
            continue
        ours = next((row for row in rows if row["owner_stem"] == retail_stem), None)
        if ours is None:
            owner_mismatches.append(
                {"label": entry["label"], "retail": retail_stem,
                 "export": sorted(row["owner_stem"] for row in rows),
                 "hits": sum(entry["owners"].values())}
            )
            continue
        retail_activity = {name for name in entry["activity_names"]}
        if retail_activity and ours["activity_name"] not in retail_activity:
            activity_mismatches.append(
                {"label": entry["label"], "retail": sorted(retail_activity),
                 "export": ours["activity_name"]}
            )
            continue
        agreed += 1
    return {
        "labels_witnessed": len(witnessed),
        "agreed": agreed,
        "absent_from_export": len(absent),
        "owner_mismatches": len(owner_mismatches),
        "activity_mismatches": len(activity_mismatches),
        "retail_conflicts": len(conflicts),
        "retail_owners_without_export_stem": dict(unmapped_owners),
        "examples": {
            "absent_from_export": absent[:EXAMPLES],
            "owner_mismatches": owner_mismatches[:EXAMPLES],
            "activity_mismatches": activity_mismatches[:EXAMPLES],
            "retail_conflicts": conflicts[:EXAMPLES],
        },
    }


def oracle_rows(
    witnessed: dict[str, dict[str, Any]], stem_of: dict[str, str]
) -> list[dict[str, Any]]:
    """One body's witnessed selections in the export's stem namespace."""
    rows = []
    for lowered, entry in sorted(witnessed.items()):
        for owner, hits in sorted(entry["owners"].items()):
            rows.append(
                {
                    "label": entry["label"],
                    "owner_model": owner,
                    "owner_stem": stem_of.get(owner),
                    "activity_names": sorted(entry["activity_names"]),
                    "activity_ids": sorted(entry["activity_ids"], key=int),
                    "sequences": sorted(entry["sequences"], key=int),
                    "targets": sorted(entry["targets"]),
                    "hits": hits,
                }
            )
    return rows


# --- retarget: the bone correspondence and its matrix against retail's own -------------------

#: Unreal declines to build a retarget cache entry when the two bind poses agree within this,
#: so a pair inside it is a no-op on our side whatever mode the skeleton names
#: (`BoneContainer.cpp`). Centimetres, because every exported bind is Unreal-native.
UNREAL_SKIP_CM = 0.001

#: The scale and angle a re-derived matrix has to land within to read as the same transform.
SCALE_TOLERANCE = 1e-4
ANGLE_TOLERANCE_DEGREES = 0.05

#: Retail states its binds in inches; the capture's matrices carry translation in the same unit.
INCH_TO_CM = 2.54


def _length(v: Iterable[float]) -> float:
    return math.sqrt(sum(x * x for x in v))


def _difference(a: Iterable[float], b: Iterable[float]) -> tuple[float, float, float]:
    return (b[0] - a[0], b[1] - a[1], b[2] - a[2])


def retail_matrix(text: str) -> tuple[list[list[float]], tuple[float, float, float]]:
    """One captured `matrix3x4` as (3x3 rows, translation).

    Row-major, translation in the fourth column, which is how `capture_rig_remap` writes the
    twelve floats it reads off record `+0x08`.
    """
    m = [float(x) for x in text.split(";")]
    return [m[0:3], m[4:7], m[8:11]], (m[3], m[7], m[11])


def derive_transform(a: Iterable[float], b: Iterable[float]) -> dict[str, Any]:
    """Retail's builder rule evaluated on OUR two parent-relative binds.

    `animation_and_movers.md` A.4b: identity when the two positions are equal, a pure
    translation when either is at the origin, otherwise axis-angle from `cross(a, b)` and
    `atan2` scaled by `|b|/|a|`. Only the invariants are answered -- scale, the angle between
    the two bind directions, and the translation magnitude -- because `source_to_unreal`'s Y
    negation is a REFLECTION: it preserves an angle and flips the axis it turns about, so a
    full-matrix comparison against a capture states a sign difference as a finding.
    """
    la, lb = _length(a), _length(b)
    delta = _difference(a, b)
    if la <= UNREAL_SKIP_CM or lb <= UNREAL_SKIP_CM:
        return {"branch": "translation", "scale": 1.0, "angle_degrees": 0.0,
                "translation_cm": _length(delta)}
    dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
    cross = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
    return {
        "branch": "axis_angle",
        "scale": lb / la,
        "angle_degrees": math.degrees(math.atan2(_length(cross), dot)),
        "translation_cm": 0.0,
        "separation_cm": _length(delta),
    }


def read_retail_transform(text: str) -> dict[str, Any]:
    """The same three invariants, read back off a captured matrix.

    A similarity carries one common row norm; dividing it out leaves a rotation whose trace
    answers the angle. Translation is stated in retail's own unit and converted, so the two
    sides are comparable without touching the rotation's frame.
    """
    rows, translation = retail_matrix(text)
    norms = [_length(row) for row in rows]
    scale = sum(norms) / 3.0
    angle = 0.0
    if scale > 1e-9:
        trace = sum(rows[i][i] for i in range(3)) / scale
        angle = math.degrees(math.acos(max(-1.0, min(1.0, (trace - 1.0) / 2.0))))
    return {
        "scale": scale,
        "angle_degrees": angle,
        "translation_cm": _length(translation) * INCH_TO_CM,
        "row_norm_spread": max(norms) - min(norms),
    }


class ContainerBinds:
    """Every exported container's bind pose, read once each and only when a row names one."""

    def __init__(self, export_root: Path) -> None:
        self._root = export_root / "npc"
        self._cache: dict[str, dict[str, Any] | None] = {}

    def get(self, stem: str) -> dict[str, Any] | None:
        """One stem's binds and its clips' driven bone set, or None when no container exists.

        A body writes `npc/<stem>.eskm` and a bank `npc/banks/<stem>.eskm`; the stem alone does
        not say which, so both are tried.
        """
        if stem in self._cache:
            return self._cache[stem]
        # Imported here for the same reason `install` is: the joins above are pure and tested
        # without an export on disk.
        from elysium_pipeline.formats import eskm

        value = None
        for path in (self._root / f"{stem}.eskm", self._root / "banks" / f"{stem}.eskm"):
            if not path.is_file():
                continue
            blob = eskm.read(path)
            rows = eskm.bone_locals(blob)
            driven: set[int] = set()
            for bones in eskm.clip_track_bones(blob).values():
                driven |= bones
            value = {
                "rows": rows,
                "fold": {row[0].lower(): i for i, row in enumerate(rows)},
                "driven": driven,
            }
            break
        self._cache[stem] = value
        return value


def compare_retarget(
    remap_rows: Iterable[dict[str, str]],
    binds: ContainerBinds,
    stem_of: dict[str, str],
) -> dict[str, Any]:
    """The captured remap table against this repository's own binds and baked tracks.

    Three questions, in the order a defect is likely to hide in them: which bones a bank drives
    at all, whether the pairs retail leaves alone are pairs we leave alone, and whether the
    transform the rule derives from our binds is the transform retail wrote.

    A row whose including model or bank is an include-DAG hub the export writes no container for
    is **not applicable** rather than a miss: retail composes its chain hop by hop and this
    repository retargets a bank straight onto the playing mesh, so an intermediate hop's remap
    describes a stage that does not exist here.
    """
    inapplicable: Counter = Counter()
    driven_agree = 0
    undriven_agree = 0
    undriven_we_carry: list[dict[str, Any]] = []
    mapped_absent: list[dict[str, Any]] = []
    mapped_silent: list[dict[str, Any]] = []
    copy_agree = 0
    copy_diverges: list[dict[str, Any]] = []
    transform_agree = 0
    transform_diverges: list[dict[str, Any]] = []
    chain_rebase: list[dict[str, Any]] = []
    copy_separations: list[float] = []
    transform_separations: list[float] = []
    # One row per applicable (body, bank, bone), for a runtime test to replay against the BAKED
    # assets: the binds below are the containers', so a bake that wrote something else fails
    # against them rather than against a second derivation of the same numbers.
    fixture: dict[tuple[str, str, str], dict[str, Any]] = {}
    probe_clip: dict[str, str] = {}

    for row in remap_rows:
        body = stem_of.get(model_key(row["including_model"]))
        bank = stem_of.get(model_key(row["bank"]))
        if body is None:
            inapplicable[f"including model has no container: {model_key(row['including_model'])}"] += 1
            continue
        if bank is None:
            inapplicable[f"bank has no container: {model_key(row['bank'])}"] += 1
            continue
        body_binds = binds.get(body)
        bank_binds = binds.get(bank)
        if body_binds is None or bank_binds is None:
            inapplicable["container absent from the export"] += 1
            continue
        where = {"body": body, "bank": bank, "bone": row["body_bone"]}

        if row["mode"] == "1":
            chain_rebase.append({**where, "chain_start": row["chain_start"],
                                 "chain_end": row["chain_end"]})

        # A. which bones the bank drives at all.
        if row["bank_bone_index"] == "-1":
            if bank_binds["fold"].get(row["body_bone"].lower()) is None:
                undriven_agree += 1
            else:
                undriven_we_carry.append(where)
            continue
        source = bank_binds["fold"].get(row["bank_bone"].lower())
        target = body_binds["fold"].get(row["body_bone"].lower())
        if source is None:
            mapped_absent.append({**where, "bank_bone": row["bank_bone"]})
            continue
        if target is None:
            mapped_absent.append({**where, "bank_bone": row["bank_bone"], "missing": "body"})
            continue
        if source not in bank_binds["driven"]:
            mapped_silent.append({**where, "bank_bone": row["bank_bone"]})
        else:
            driven_agree += 1

        a = bank_binds["rows"][source][2]
        b = body_binds["rows"][target][2]
        separation = _length(_difference(a, b))
        theirs_row = read_retail_transform(row["matrix3x4"])
        fixture[(body, bank, row["body_bone"])] = {
            "body": body, "bank": bank,
            "body_bone": body_binds["rows"][target][0],
            "bank_bone": bank_binds["rows"][source][0],
            "retail_copies": row["sub"] == "0",
            "bank_bind": [round(v, 6) for v in a],
            "body_bind": [round(v, 6) for v in b],
            "separation_cm": round(separation, 6),
            "retail_scale": round(theirs_row["scale"], 6),
            "retail_translation_cm": round(theirs_row["translation_cm"], 6),
        }
        probe_clip.setdefault(bank, "")

        # B and C. retail states which of the two it did in `sub`: clear copies, set transforms.
        if row["sub"] == "0":
            copy_separations.append(separation)
            if separation <= UNREAL_SKIP_CM:
                copy_agree += 1
            else:
                copy_diverges.append({**where, "separation_cm": round(separation, 6)})
            continue

        transform_separations.append(separation)
        ours = derive_transform(a, b)
        theirs = read_retail_transform(row["matrix3x4"])
        scale_error = abs(ours["scale"] - theirs["scale"])
        angle_error = abs(ours["angle_degrees"] - theirs["angle_degrees"])
        translation_error = abs(ours["translation_cm"] - theirs["translation_cm"])
        # Retail wrote a rotation only where it took the axis-angle branch; a unit-scale,
        # zero-angle matrix carrying a translation IS the origin branch, whatever our own
        # zero test answered on the same pair.
        retail_branch = ("translation"
                         if abs(theirs["scale"] - 1.0) <= SCALE_TOLERANCE
                         and theirs["angle_degrees"] <= ANGLE_TOLERANCE_DEGREES
                         and theirs["translation_cm"] > UNREAL_SKIP_CM
                         else "axis_angle")
        if retail_branch != ours["branch"]:
            transform_diverges.append({
                **where, "kind": "branch",
                "retail": retail_branch, "ours": ours["branch"],
                "retail_translation_cm": round(theirs["translation_cm"], 5),
                "our_scale": round(ours["scale"], 5),
                "separation_cm": round(separation, 5),
                "bank_bind_cm": round(_length(a), 5), "body_bind_cm": round(_length(b), 5),
            })
        elif retail_branch == "translation":
            if translation_error > UNREAL_SKIP_CM:
                transform_diverges.append({
                    **where, "kind": "translation",
                    "retail_cm": round(theirs["translation_cm"], 5),
                    "ours_cm": round(ours["translation_cm"], 5),
                })
            else:
                transform_agree += 1
        elif scale_error > SCALE_TOLERANCE:
            transform_diverges.append({
                **where, "kind": "scale",
                "retail": round(theirs["scale"], 5), "ours": round(ours["scale"], 5),
            })
        elif angle_error > ANGLE_TOLERANCE_DEGREES:
            transform_diverges.append({
                **where, "kind": "angle",
                "retail": round(theirs["angle_degrees"], 4),
                "ours": round(ours["angle_degrees"], 4),
            })
        else:
            transform_agree += 1

    # Retail's own "the binds differ" epsilon is not a constant this repository can read, but the
    # corpus brackets it: every pair it copied is below the bracket and every pair it transformed
    # is above. Reported because Unreal's own threshold sits far below the bracket, which is what
    # makes the copy divergences above a rule difference rather than a bind-data one.
    epsilon = {
        "copied_max_cm": round(max(copy_separations), 6) if copy_separations else None,
        "transformed_min_cm": round(min(transform_separations), 6) if transform_separations else None,
        "unreal_skip_cm": UNREAL_SKIP_CM,
    }
    return {
        "rows_applicable": (undriven_agree + len(undriven_we_carry) + driven_agree
                            + len(mapped_silent) + len(mapped_absent)),
        "rows_not_applicable": sum(inapplicable.values()),
        "not_applicable_reasons": dict(inapplicable.most_common(EXAMPLES)),
        "driven": {
            "undriven_agree": undriven_agree,
            "driven_agree": driven_agree,
            "undriven_we_carry": len(undriven_we_carry),
            "mapped_absent": len(mapped_absent),
            "mapped_silent": len(mapped_silent),
            "examples": {
                "undriven_we_carry": undriven_we_carry[:EXAMPLES],
                "mapped_absent": mapped_absent[:EXAMPLES],
                "mapped_silent": mapped_silent[:EXAMPLES],
            },
        },
        "copy": {
            "agree": copy_agree,
            "diverges": len(copy_diverges),
            "examples": sorted(copy_diverges, key=lambda r: -r["separation_cm"])[:EXAMPLES],
        },
        "transform": {
            "agree": transform_agree,
            "diverges": len(transform_diverges),
            "examples": transform_diverges[:EXAMPLES],
        },
        "chain_rebase_rows": chain_rebase,
        "retail_epsilon_bracket_cm": epsilon,
        "rows": sorted(fixture.values(), key=lambda r: (r["body"], r["bank"], r["body_bone"])),
        "banks": sorted(probe_clip),
    }


# --- the run --------------------------------------------------------------------------------


def run(session: Path, export_root: Path) -> dict[str, Any]:
    # Imported here rather than at module scope: `install` resolves the VtMB root the
    # moment it loads, and the comparisons above are pure and tested without one.
    from elysium_pipeline.formats import install

    index = install.build_index(dirs=("models",), verbose=False)

    def load(key: str) -> bytes | None:
        return install.read(index, model_key(key)) or None

    live_rows = read_csv(session / "sequence_map.csv")
    if not live_rows:
        raise FileNotFoundError(session / "sequence_map.csv")
    resolution = read_csv(session / "resolution.csv")
    ownership = read_csv(session / "ownership.csv")
    remap = read_csv(session / "rig_remap.csv")
    export_index = load_export_index(export_root)
    stem_of = stems_by_model(export_index)

    live_by_body: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in live_rows:
        live_by_body[model_key(row["body_model"])].append(row)
    witnessed = witnessed_owners(resolution, ownership)

    bank_stems = set((export_index.get("banks") or {}).keys())
    numbering: dict[str, Any] = {}
    flats: dict[str, list[dict[str, Any]]] = {}
    unreadable: list[str] = []
    for body, rows in sorted(live_by_body.items()):
        if load(body) is None:
            unreadable.append(body)
            continue
        offline = offline_numbering(load, body)
        ordering, report = best_numbering(rows, offline)
        numbering[body] = {"ordering": ordering, **report}
        flats[body] = offline[ordering]

    def flat_for(body: str) -> list[dict[str, Any]] | None:
        if body not in flats and body not in unreadable:
            if load(body) is None:
                unreadable.append(body)
            else:
                # A body the session selected on without reading its live tables has no
                # numbering to verify; its tree is still what decides which banks it reaches.
                flats[body] = offline_numbering(load, body)["nodedup"]
        return flats.get(body)

    ownership_report: dict[str, Any] = {}
    oracle: dict[str, Any] = {}
    no_stem: list[str] = []
    no_clip_map: list[str] = []
    for body, labels in sorted(witnessed.items()):
        stem = stem_of.get(body)
        if stem is None:
            no_stem.append(body)
            continue
        if stem in bank_stems:
            # A bank asked about its own numbers (an ownership chain's head) has no clip
            # map of its own; its labels are checked through every body that includes it.
            continue
        try:
            exported = export_clip_owners(export_root, stem)
        except FileNotFoundError:
            no_clip_map.append(stem)
            continue
        flat = flat_for(body)
        if flat is None:
            continue
        admitted, rejected = admissible_witnesses(labels, reachable_owners(flat))
        ownership_report[stem] = {
            **compare_ownership(admitted, exported, stem_of),
            "unreachable_witnesses": len(rejected),
            "unreachable_examples": [
                {"label": entry["label"], "owners": sorted(entry["owners"])}
                for entry in list(rejected.values())[:EXAMPLES]
            ],
        }
        oracle[stem] = {"model": body, "selections": oracle_rows(admitted, stem_of)}

    collisions = {
        stem_of[body]: label_collisions(flat)
        for body, flat in sorted(flats.items())
        if body in stem_of and stem_of[body] not in bank_stems
    }

    # The export's own first-wins rule against the flat order retail built: a label the
    # export assigns to a later bank than the one the space reaches first is a drop
    # upstream of the clip map, not a numbering difference.
    first_owner_report: dict[str, Any] = {}
    sequence_report: dict[str, Any] = {}
    for body, flat in sorted(flats.items()):
        stem = stem_of.get(body)
        if stem is None or stem in bank_stems:
            continue
        try:
            exported = export_clip_owners(export_root, stem)
        except FileNotFoundError:
            continue
        # The numbers the export derived offline against the ones the running game handed out.
        if body in live_by_body:
            sequence_report[stem] = compare_sequence_numbers(
                exported, live_by_body[body], stem_of)
        firsts = first_occurrence_owners(flat)
        mismatches = []
        for lowered, first in sorted(firsts.items()):
            rows = exported.get(lowered)
            ours = rows[0] if rows else None
            retail_stem = stem_of.get(first["owner_model"])
            if ours is None or retail_stem is None:
                continue
            if ours["owner_stem"] != retail_stem:
                mismatches.append(
                    {"label": first["label"], "global_index": first["global_index"],
                     "retail": retail_stem, "export": ours["owner_stem"]}
                )
        first_owner_report[stem] = {
            "labels_compared": len(firsts),
            "owner_mismatches": len(mismatches),
            "examples": mismatches[:EXAMPLES],
        }

    divergences = (
        sum(r["owner_mismatches"] + r["label_mismatches"] for r in numbering.values())
        + sum(
            r["owner_mismatches"] + r["activity_mismatches"] + r["absent_from_export"]
            for r in ownership_report.values()
        )
        + sum(r["owner_mismatches"] for r in first_owner_report.values())
        + sum(r["owner_mismatches"] + r["label_mismatches"] for r in sequence_report.values())
    )
    retarget = compare_retarget(remap, ContainerBinds(export_root), stem_of) if remap else {}
    report = {
        "schema": "elysium.rig-oracle",
        "version": 1,
        "session": str(session),
        "export_root": str(export_root),
        "divergences": divergences,
        "bodies_unreadable_from_install": unreadable,
        "bodies_without_export_stem": no_stem,
        "stems_without_clip_map": no_clip_map,
        "numbering": numbering,
        "sequence_numbers": sequence_report,
        "first_owner": first_owner_report,
        "collisions": collisions,
        "ownership": ownership_report,
        "retarget": retarget,
        "oracle": oracle,
    }
    (session / REPORT_NAME).write_text(
        json.dumps(report, indent=1, sort_keys=True) + "\n", encoding="utf-8"
    )
    return report


def print_summary(report: dict[str, Any]) -> None:
    print(f"Rig parity: {report['session']}")
    print("  numbering (offline flat space vs live sequence_map.csv)")
    for body, entry in report["numbering"].items():
        flag = "ok " if not (entry["owner_mismatches"] or entry["label_mismatches"]) else "DIV"
        print(
            f"    {flag} {body}: {entry['ordering']} {entry['offline_total']} vs live "
            f"{entry['live_total']}, owner {entry['owner_mismatches']} label "
            f"{entry['label_mismatches']}"
        )
    print("  sequence numbers (export clip map vs the live flat space)")
    for stem, entry in report["sequence_numbers"].items():
        bad = entry["owner_mismatches"] + entry["label_mismatches"]
        flag = "ok " if not bad else "DIV"
        print(
            f"    {flag} {stem}: {entry['rows_checked']} row(s) checked, owner "
            f"{entry['owner_mismatches']}, label {entry['label_mismatches']}, unnumbered "
            f"{entry['rows_unnumbered']}, off the live table "
            f"{entry['numbers_absent_from_live_table']}"
        )
    print("  first owner (export clip map vs the label's first global number)")
    for stem, entry in report["first_owner"].items():
        flag = "ok " if not entry["owner_mismatches"] else "DIV"
        print(f"    {flag} {stem}: {entry['owner_mismatches']} of {entry['labels_compared']}")
    print("  ownership (export clip map vs retail's committed selections)")
    for stem, entry in report["ownership"].items():
        bad = entry["owner_mismatches"] + entry["activity_mismatches"] + entry["absent_from_export"]
        flag = "ok " if not bad else "DIV"
        print(
            f"    {flag} {stem}: agreed {entry['agreed']}/{entry['labels_witnessed']}, owner "
            f"{entry['owner_mismatches']}, activity {entry['activity_mismatches']}, absent "
            f"{entry['absent_from_export']}, retail conflicts {entry['retail_conflicts']}, "
            f"unreachable {entry['unreachable_witnesses']}"
        )
    print("  label collisions in the flat space (retail keeps every copy; the clip map keeps one)")
    for stem, entry in report["collisions"].items():
        print(
            f"    {stem}: {entry['labels_repeated_across_banks']} labels repeated across banks, "
            f"{entry['same_activity']} same activity, {entry['different_activity']} different"
        )
    retarget = report.get("retarget") or {}
    if retarget:
        driven = retarget["driven"]
        print("  retarget: bone correspondence and matrix vs the captured remap table")
        print(f"    rows applicable {retarget['rows_applicable']}, "
              f"not applicable {retarget['rows_not_applicable']} (chain hops we do not take)")
        print(f"    driven   agree {driven['driven_agree']} + undriven {driven['undriven_agree']}; "
              f"we carry an undriven bone {driven['undriven_we_carry']}, "
              f"bank bone absent {driven['mapped_absent']}, silenced {driven['mapped_silent']}")
        print(f"    copy     agree {retarget['copy']['agree']}, "
              f"diverge {retarget['copy']['diverges']}")
        print(f"    transform agree {retarget['transform']['agree']}, "
              f"diverge {retarget['transform']['diverges']}")
        bracket = retarget["retail_epsilon_bracket_cm"]
        print(f"    retail's 'binds differ' epsilon is between {bracket['copied_max_cm']} and "
              f"{bracket['transformed_min_cm']} cm; Unreal skips below {bracket['unreal_skip_cm']} cm")
        for kind in ("copy", "transform"):
            for row in retarget[kind]["examples"][:6]:
                print(f"      {kind}: {row}")
    for key in ("bodies_unreadable_from_install", "bodies_without_export_stem",
                "stems_without_clip_map"):
        if report[key]:
            print(f"  {key}: {', '.join(report[key])}")
    print(f"  divergences {report['divergences']}; report {Path(report['session']) / REPORT_NAME}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path,
                        help="A capture directory; the newest with a sequence_map.csv when omitted.")
    parser.add_argument("--export", type=Path,
                        help="The export root; $ELYSIUM_EXPORT_ROOT when omitted.")
    arguments = parser.parse_args()
    session = arguments.session.resolve() if arguments.session else latest_session()
    if not session.is_dir():
        raise NotADirectoryError(session)
    if arguments.export:
        export_root = arguments.export.resolve()
    else:
        from elysium_pipeline.paths import export_root as default_export_root

        export_root = default_export_root()
    report = run(session, export_root)
    print_summary(report)
    return 1 if report["divergences"] else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:  # noqa: BLE001 - the CLI reports its own failure
        print(f"WARNING - rig parity failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
