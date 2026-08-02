"""Join every fired contribution to the installed bytes, the export and the inventory.

Usage:
    uv run elysium research verify_source_join <session> [<session> ...]

Reads a finalized capture database read-only and resolves each fired
contribution's runtime identity — owning model checksum plus owner-local
sequence or animation index — against the three offline things CAP4 has to
compare it with: the patch-first installed `.mdl`, the current exported
animation, and the CAP0.5 player inventory. Nothing is written into the
database; the join depends on this machine's install and export state, and an
evidence file must not carry a claim about a moment outside itself.

Three bounds travel with the report.

Coverage is stated twice. A run fires a few dozen distinct identities across a
few hundred thousand records, so a count of identities and a count of records
answer different questions and neither substitutes for the other. A one-record
identity and a thirty-thousand-record one are the same row in the first and two
orders of magnitude apart in the second.

Only the install arm can fail. An identity that resolves to no installed bytes,
or whose captured pointer misses the array position the installed header
declares, is a defect: every later comparison would be reading the wrong bytes.
An identity the current export cannot name, or one the inventory does not cover,
is a measured shortfall reported with the reason it holds — that is the finding
CAP4.4 ranks, not a fault in the capture.

The export arm characterizes the exporter deliberately. `local_sequences` drops
the sequence index, keeps the first sequence to claim a lowercased label, skips
one whose base blend cell is out of range, and bakes cell [0][0] alone. This
tool replays those rules over the installed image with indices preserved, so a
missing clip is attributed to the rule that dropped it rather than reported as
an unexplained absence.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import struct
from typing import Any

from research.tooling.capture.calibrate_theatre_capture import (
    DATABASE_NAME,
    compared_maps,
    open_database,
    resolve_session,
)
from research.tooling.capture.resolve_consumed_spans import (
    file_sha256,
    tool_commit,
)
from research.tooling.capture.verify_model_skeleton_census import install_key

REPORT_NAME = "source-join.json"

SEQUENCE_KIND = "SEQP"
ANIMATION_KIND = "ANIM"

EVENT_TABLE = "record_events"
DEFAULT_EVENT_TABLE = "records"

# MDLHeader displacements. docs/vtmb/mdl_v2531.md owns the table.
HEADER_CHECKSUM = 8
HEADER_NUM_LOCAL_ANIMS = 264
HEADER_LOCAL_ANIM_INDEX = 268
HEADER_NUM_LOCAL_SEQ = 272
HEADER_LOCAL_SEQ_INDEX = 276

SEQUENCE_DESCRIPTOR_BYTES = 764
ANIMATION_DESCRIPTOR_BYTES = 72

# StudioSeqDesc displacements, all relative to the descriptor base.
SEQ_LABEL_INDEX = 0
SEQ_ACTIVITY_NAME_INDEX = 4
SEQ_FLAGS = 8
SEQ_ACTIVITY = 12
SEQ_ACT_WEIGHT = 16
SEQ_NUM_BLENDS = 52
SEQ_BASE_CELL = 56
SEQ_GROUP_SIZE = 572
SEQ_PARAM_INDEX = 580

# StudioAnimDesc displacements.
ANIM_NAME_INDEX = 0
ANIM_FPS = 4
ANIM_FLAGS = 8
ANIM_FRAMES = 12
ANIM_ANIM_INDEX = 48

# The one dword the loader rewrites inside a sequence descriptor. A captured
# image differing from the installed file only here is the loader doing what
# CAP2.2 measured; differing anywhere else is a fact this run would be the
# first to see. docs/vtmb/mdl_v2531.md records the range.
SEQ_LOADER_WRITTEN = (0x0C, 0x10)

EXPORT_MANIFEST = os.path.join("npc", "npc_manifest.json")
INVENTORY_ROOT = "player-animation-inventory"
INVENTORY_PREFIX = "cap12_"

# A population whose non-zero value is a measured property of the export, the
# inventory or the loader rather than a defect, with the reason it holds.
ACCOUNTED: dict[str, str] = {
    "descriptor_differs_only_at_0xc": (
        "The loader rewrites StudioSeqDesc+0xc in place, so a captured "
        "descriptor differing there and nowhere else is the fixup CAP2.2 "
        "measured rather than a disagreement about the source bytes."
    ),
    "export_has_no_such_owner": (
        "The export seeds from entity lists and clandoc bodies, so a bank a "
        "map animates that no seed reaches is never exported. The owner is "
        "named rather than counted."
    ),
    "label_owned_by_another_sequence_index": (
        "local_sequences dedupes by lowercased label, first wins, so a later "
        "sequence sharing a label is dropped and the exported clip carries "
        "the earlier one's animation."
    ),
    "base_cell_out_of_range": (
        "local_sequences skips a sequence whose anim[0][0] falls outside "
        "NumLocalAnims, so the label never reaches a clip."
    ),
    "label_is_empty": (
        "A sequence with no label is skipped by local_sequences and has no "
        "name an exported clip could be keyed on."
    ),
    "clip_baked_no_animation": (
        "The label survived local_sequences but baked no animation, so the "
        "manifest carries no clip for it."
    ),
    "blend_cell_not_exported": (
        "local_sequences bakes blend cell [0][0] alone, so every other cell a "
        "multi-blend sequence fires has no exported counterpart."
    ),
    "inventory_does_not_cover_this_owner": (
        "The CAP0.5 inventory is seeded from clandoc000.txt player bodies and "
        "their include tree, so a cinematic, scenery or NPC-only owner is "
        "outside it by construction."
    ),
}

# A population that means a later comparison would read the wrong bytes.
DEFECTS = (
    "install_path_absent",
    "installed_checksum_differs",
    "index_outside_declared_count",
    "descriptor_offset_disagrees",
    "descriptor_differs_elsewhere",
    "offset_inconsistent_across_records",
    "export_clip_meta_disagrees",
    "inventory_missing_a_declared_index",
    "inventory_descriptor_digest_disagrees",
    "fired_cell_not_declared",
)


def address(value: int | None) -> str | None:
    return None if value is None else f"0x{value:08x}"


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _i16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<h", data, offset)[0]


def _f32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def _cstr(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        return ""
    end = data.find(b"\0", offset)
    end = len(data) if end < 0 else end
    return data[offset:end].decode("ascii", "replace")


def _cstr_rel(data: bytes, base: int, field: int) -> str:
    return _cstr(data, base + _i32(data, base + field))


def event_table(connection: sqlite3.Connection) -> str:
    """The table carrying event rows without the payload the view materializes.

    A compacted database exposes `records` as a view over `record_events` and
    `payloads`; aggregating the view drags every payload off disk for counts
    that never read one.
    """
    names = {
        name
        for (name,) in connection.execute(
            "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
        )
    }
    return EVENT_TABLE if EVENT_TABLE in names else DEFAULT_EVENT_TABLE


def support(connection: sqlite3.Connection) -> dict[str, Any]:
    objects = {
        name
        for (name,) in connection.execute(
            "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
        )
    }
    table = event_table(connection)
    columns = {
        row[1] for row in connection.execute(f"PRAGMA table_info({table})")
    }
    return {
        "event_table": table,
        "carries_contributions": {
            "owner_checksum",
            "sequence_index",
            "animation_index",
            "sequence_descriptor",
            "animation_descriptor",
            "owner_studio_hdr",
        }
        <= columns,
        "carries_census": "model_headers" in objects,
        "carries_images": "model_images" in objects,
    }


def prepare(connection: sqlite3.Connection, flags: dict[str, Any]) -> None:
    """One row per distinct fired identity, with its own multiplicity.

    The grouping carries `image_offset`, so an identity the runtime reached at
    two different displacements splits into two rows and is named rather than
    collapsed into whichever the group happened to keep.

    Every grouping term is the underlying expression, never the output alias.
    An event row carries its own `checksum` — the drawn model's, not the
    contribution owner's — so `GROUP BY checksum` binds to that column instead
    of the alias and silently groups unrelated owners together.
    """
    table = flags["event_table"]
    connection.execute(
        f"""
        CREATE TEMP TABLE fired_identity AS
        SELECT owner_checksum AS checksum,
               kind AS record_kind,
               CASE WHEN kind = '{SEQUENCE_KIND}' THEN sequence_index
                    ELSE animation_index END AS idx,
               CASE WHEN kind = '{SEQUENCE_KIND}'
                    THEN sequence_descriptor - owner_studio_hdr
                    ELSE animation_descriptor - owner_studio_hdr END
                    AS image_offset,
               count(*) AS records,
               count(DISTINCT generation) AS pose_builds,
               count(DISTINCT generation_entity) AS actors,
               min(qpc) AS first_qpc,
               max(qpc) AS last_qpc
        FROM {table}
        WHERE kind IN ('{SEQUENCE_KIND}', '{ANIMATION_KIND}')
        GROUP BY owner_checksum,
                 kind,
                 CASE WHEN kind = '{SEQUENCE_KIND}' THEN sequence_index
                      ELSE animation_index END,
                 CASE WHEN kind = '{SEQUENCE_KIND}'
                      THEN sequence_descriptor - owner_studio_hdr
                      ELSE animation_descriptor - owner_studio_hdr END
        """
    )
    connection.execute(
        "CREATE INDEX temp.fired_identity_checksum ON fired_identity(checksum)"
    )


def corpus(connection: sqlite3.Connection) -> dict[str, Any]:
    """What the run fired, by distinct identity and by record."""
    rows = connection.execute(
        "SELECT record_kind, count(*), sum(records), count(DISTINCT checksum) "
        "FROM fired_identity GROUP BY record_kind"
    ).fetchall()
    by_kind = {
        kind: {"identities": identities, "records": records, "owners": owners}
        for kind, identities, records, owners in rows
    }
    inconsistent = connection.execute(
        "SELECT count(*) FROM (SELECT checksum, record_kind, idx FROM fired_identity "
        "GROUP BY checksum, record_kind, idx HAVING count(*) > 1)"
    ).fetchone()[0]
    return {
        "owners": connection.execute(
            "SELECT count(DISTINCT checksum) FROM fired_identity"
        ).fetchone()[0],
        "identities": connection.execute(
            "SELECT count(*) FROM fired_identity"
        ).fetchone()[0],
        "records": connection.execute(
            "SELECT sum(records) FROM fired_identity"
        ).fetchone()[0]
        or 0,
        "sequence": by_kind.get(
            SEQUENCE_KIND, {"identities": 0, "records": 0, "owners": 0}
        ),
        "animation": by_kind.get(
            ANIMATION_KIND, {"identities": 0, "records": 0, "owners": 0}
        ),
        "offset_inconsistent_across_records": inconsistent,
    }


def _owner_names(connection: sqlite3.Connection) -> dict[int, str]:
    """Checksum to the model name the census recorded for it.

    Read from `model_headers` rather than the spine's `model_identity`, so a
    capture that has not been indexed still answers.
    """
    return {
        int(checksum): name
        for checksum, name in connection.execute(
            "SELECT checksum, min(model_name) FROM model_headers "
            "GROUP BY checksum"
        )
    }


def _captured_images(connection: sqlite3.Connection) -> dict[int, bytes]:
    return {
        int(checksum): bytes(image)
        for checksum, image in connection.execute(
            "SELECT checksum, image FROM model_images"
        )
    }


def _identity_rows(connection: sqlite3.Connection) -> list[dict[str, Any]]:
    return [
        {
            "checksum": int(checksum),
            "kind": kind,
            "index": idx,
            "image_offset": offset,
            "records": records,
            "pose_builds": builds,
            "actors": actors,
        }
        for checksum, kind, idx, offset, records, builds, actors
        in connection.execute(
            "SELECT checksum, record_kind, idx, image_offset, records, "
            "pose_builds, actors FROM fired_identity "
            "ORDER BY checksum, record_kind, idx"
        )
    ]


def _descriptor_difference(
    captured: bytes | None, installed: bytes, base: int, stride: int, kind: str
) -> str:
    """How a captured descriptor differs from the installed one, by class."""
    if captured is None or len(captured) < base + stride:
        return "no_captured_image"
    if len(installed) < base + stride:
        return "installed_image_too_short"
    left = captured[base : base + stride]
    right = installed[base : base + stride]
    if left == right:
        return "identical"
    differing = {offset for offset in range(stride) if left[offset] != right[offset]}
    if kind == SEQUENCE_KIND:
        low, high = SEQ_LOADER_WRITTEN
        if differing and all(low <= offset < high for offset in differing):
            return "differs_only_at_0xc"
    return "differs_elsewhere"


def _decode_sequence(data: bytes, base: int) -> dict[str, Any]:
    return {
        "label": _cstr_rel(data, base, SEQ_LABEL_INDEX),
        "activity_name": _cstr_rel(data, base, SEQ_ACTIVITY_NAME_INDEX),
        "flags": _i32(data, base + SEQ_FLAGS),
        "activity": _i32(data, base + SEQ_ACTIVITY),
        "act_weight": _i32(data, base + SEQ_ACT_WEIGHT),
        "num_blends": _i32(data, base + SEQ_NUM_BLENDS),
        "base_cell": _i16(data, base + SEQ_BASE_CELL),
        "group_size": [
            _i32(data, base + SEQ_GROUP_SIZE),
            _i32(data, base + SEQ_GROUP_SIZE + 4),
        ],
        "param_index": _i32(data, base + SEQ_PARAM_INDEX),
    }


def _decode_animation(data: bytes, base: int) -> dict[str, Any]:
    return {
        "name": _cstr_rel(data, base, ANIM_NAME_INDEX),
        "fps": round(_f32(data, base + ANIM_FPS), 4),
        "flags": _i32(data, base + ANIM_FLAGS),
        "frames": _i32(data, base + ANIM_FRAMES),
        "anim_index": _i32(data, base + ANIM_ANIM_INDEX),
    }


def exporter_outcomes(data: bytes) -> dict[int, dict[str, Any]]:
    """Replay `local_sequences` over an installed image with indices preserved.

    The exporter's own walk discards the index it iterated, so a clip it dropped
    cannot be attributed afterwards. Repeating the three rules here — empty
    label, first-wins lowercased dedup, base cell inside NumLocalAnims — names
    which rule dropped which sequence index.
    """
    count = _i32(data, HEADER_NUM_LOCAL_SEQ)
    base = _i32(data, HEADER_LOCAL_SEQ_INDEX)
    anims = _i32(data, HEADER_NUM_LOCAL_ANIMS)
    outcomes: dict[int, dict[str, Any]] = {}
    winner: dict[str, int] = {}
    for index in range(count):
        start = base + index * SEQUENCE_DESCRIPTOR_BYTES
        if start + SEQUENCE_DESCRIPTOR_BYTES > len(data):
            outcomes[index] = {"outcome": "descriptor_outside_image"}
            continue
        label = _cstr_rel(data, start, SEQ_LABEL_INDEX)
        cell = _i16(data, start + SEQ_BASE_CELL)
        key = label.lower()
        if not label:
            outcomes[index] = {"outcome": "label_is_empty", "label": label}
            continue
        if key in winner:
            outcomes[index] = {
                "outcome": "label_owned_by_another_sequence_index",
                "label": label,
                "owned_by": winner[key],
            }
            continue
        if not 0 <= cell < anims:
            outcomes[index] = {
                "outcome": "base_cell_out_of_range",
                "label": label,
                "base_cell": cell,
                "declared_animations": anims,
            }
            continue
        winner[key] = index
        outcomes[index] = {"outcome": "kept", "label": label, "base_cell": cell}
    return outcomes


def patch_first_reader() -> tuple[Any, str]:
    """A `key -> {data, layer, path}` reader over the patch-first install.

    Isolated from the arm that uses it so a regression can supply its own
    synthetic install without a game tree, and so an absent install fails at
    one place rather than inside the walk.
    """
    from elysium_pipeline.formats import install

    index = install.build_index(dirs=("models",), verbose=False)
    patch_root = os.path.normcase(str(install.PATCH))
    game_root = os.path.normcase(str(install.GAME))

    def read(key: str) -> dict[str, Any] | None:
        entry = index.get(key)
        data = install.read(index, key)
        if data is None:
            return None
        layer = path = None
        if entry is not None:
            kind, value = entry
            if kind == "loose":
                path = str(value)
                normalized = os.path.normcase(path)
                layer = (
                    "patch"
                    if normalized.startswith(patch_root)
                    else "retail"
                    if normalized.startswith(game_root)
                    else "loose"
                )
            else:
                layer = "vpk"
        return {"data": data, "layer": layer, "path": path}

    return read, str(install.GAME_ROOT)


def install_arm(
    connection: sqlite3.Connection,
    flags: dict[str, Any],
    reader: Any = None,
    vtmb_root: str | None = None,
) -> tuple[dict[str, Any], dict[int, dict[str, Any]], list[dict[str, Any]]]:
    """Resolve every fired identity against the patch-first installed bytes.

    Two checks make this a join rather than a lookup. The captured pointer's
    image displacement must equal the array position the *installed* header
    declares — CAP2.4 ran that predicate against the captured image, and running
    it against a copy of the header the probe never touched is what turns
    agreement into evidence. And the descriptor bytes themselves must match
    between the two copies, outside the one dword the loader rewrites.
    """
    if reader is None:
        # An absent install is an answer, not a crash — but it is a failing
        # answer here, because resolving the installed bytes is the task.
        try:
            reader, vtmb_root = patch_first_reader()
        except Exception as error:  # noqa: BLE001
            return (
                {"available": False, "reason": f"{type(error).__name__}: {error}"},
                {},
                [],
            )

    names = _owner_names(connection) if flags["carries_census"] else {}
    images = _captured_images(connection) if flags["carries_images"] else {}
    rows = _identity_rows(connection)

    owners: dict[int, dict[str, Any]] = {}
    for row in rows:
        checksum = row["checksum"]
        if checksum in owners:
            continue
        name = names.get(checksum)
        owner: dict[str, Any] = {
            "checksum": address(checksum),
            "model_name": name,
            "install_key": install_key(name) if name else None,
            "resolved_path": None,
            "layer": None,
            "installed_bytes": None,
            "installed_sha256": None,
            "declared_sequences": None,
            "declared_animations": None,
            "unresolved": None,
        }
        owners[checksum] = owner
        if not name:
            owner["unresolved"] = "install_path_absent"
            owner["reason"] = "no census observation names this checksum"
            continue
        found = reader(owner["install_key"])
        if found is None:
            owner["unresolved"] = "install_path_absent"
            owner["reason"] = "the patch-first install has no such path"
            continue
        data = found["data"]
        owner["layer"] = found.get("layer")
        owner["resolved_path"] = found.get("path")
        owner["installed_bytes"] = len(data)
        owner["installed_sha256"] = hashlib.sha256(data).hexdigest()
        if len(data) < HEADER_LOCAL_SEQ_INDEX + 4:
            owner["unresolved"] = "installed_checksum_differs"
            owner["reason"] = "the installed file is shorter than the header"
            continue
        installed_checksum = struct.unpack_from("<I", data, HEADER_CHECKSUM)[0]
        if installed_checksum != checksum:
            owner["unresolved"] = "installed_checksum_differs"
            owner["reason"] = (
                "the installed file carries checksum "
                + address(installed_checksum)
            )
            continue
        owner["declared_sequences"] = _i32(data, HEADER_NUM_LOCAL_SEQ)
        owner["declared_animations"] = _i32(data, HEADER_NUM_LOCAL_ANIMS)
        owner["_data"] = data
        owner["_outcomes"] = exporter_outcomes(data)

    resolved: list[dict[str, Any]] = []
    counts = {name: 0 for name in DEFECTS}
    counts["descriptor_differs_only_at_0xc"] = 0
    record_counts = dict(counts)
    # How each joined descriptor compared against the copy the probe carried
    # out of the process. Informational rather than a population the verdict
    # judges, so it is reported apart from `counts`.
    descriptor_bytes: dict[str, int] = {}
    joined = joined_records = 0

    for row in rows:
        owner = owners[row["checksum"]]
        entry = dict(row)
        entry["checksum"] = address(row["checksum"])
        entry["model_name"] = owner["model_name"]
        entry["image_offset_hex"] = address(row["image_offset"])
        fault: str | None = owner["unresolved"]
        data = owner.get("_data")
        if data is None:
            entry["fault"] = fault
            resolved.append(entry)
            if fault:
                counts[fault] += 1
                record_counts[fault] += row["records"]
            continue
        if row["kind"] == SEQUENCE_KIND:
            count = owner["declared_sequences"]
            base = _i32(data, HEADER_LOCAL_SEQ_INDEX)
            stride = SEQUENCE_DESCRIPTOR_BYTES
        else:
            count = owner["declared_animations"]
            base = _i32(data, HEADER_LOCAL_ANIM_INDEX)
            stride = ANIMATION_DESCRIPTOR_BYTES
        entry["declared_count"] = count
        expected = base + row["index"] * stride
        entry["expected_offset"] = expected
        entry["expected_offset_hex"] = address(expected)
        if not 0 <= row["index"] < count:
            fault = "index_outside_declared_count"
        elif row["image_offset"] != expected:
            fault = "descriptor_offset_disagrees"
        elif expected + stride > len(data):
            fault = "index_outside_declared_count"
        if fault:
            entry["fault"] = fault
            counts[fault] += 1
            record_counts[fault] += row["records"]
            resolved.append(entry)
            continue
        difference = _descriptor_difference(
            images.get(row["checksum"]), data, expected, stride, row["kind"]
        )
        entry["descriptor_bytes"] = difference
        descriptor_bytes[difference] = descriptor_bytes.get(difference, 0) + 1
        if difference == "differs_elsewhere":
            entry["fault"] = "descriptor_differs_elsewhere"
            counts["descriptor_differs_elsewhere"] += 1
            record_counts["descriptor_differs_elsewhere"] += row["records"]
            resolved.append(entry)
            continue
        if difference == "differs_only_at_0xc":
            counts["descriptor_differs_only_at_0xc"] += 1
            record_counts["descriptor_differs_only_at_0xc"] += row["records"]
        entry["fault"] = None
        entry["descriptor"] = (
            _decode_sequence(data, expected)
            if row["kind"] == SEQUENCE_KIND
            else _decode_animation(data, expected)
        )
        # The evidence gate asks for the relevant input spans as raw bytes. A
        # cutscene fires a few dozen identities, so carrying every descriptor
        # verbatim costs tens of kilobytes and makes the report answerable
        # without the install it was joined against.
        entry["descriptor_hex"] = data[expected : expected + stride].hex()
        entry["descriptor_sha256"] = hashlib.sha256(
            data[expected : expected + stride]
        ).hexdigest()
        joined += 1
        joined_records += row["records"]
        resolved.append(entry)

    inconsistent = connection.execute(
        "SELECT count(*) FROM (SELECT checksum, record_kind, idx FROM fired_identity "
        "GROUP BY checksum, record_kind, idx HAVING count(*) > 1)"
    ).fetchone()[0]
    counts["offset_inconsistent_across_records"] = inconsistent

    total_records = sum(row["records"] for row in rows)
    declared = sum(
        owner["declared_sequences"] or 0
        for owner in owners.values()
        if owner.get("_data") is not None
    )
    report = {
        "available": True,
        "vtmb_root": vtmb_root,
        "owners": len(owners),
        "owners_resolved": sum(
            1 for owner in owners.values() if owner.get("_data") is not None
        ),
        "identities": len(rows),
        "records": total_records,
        "joined_identities": joined,
        "joined_records": joined_records,
        "counts": counts,
        "records_by_count": record_counts,
        "descriptor_bytes": descriptor_bytes,
        # The corpus bound. A cutscene fires a fraction of what its own owners
        # declare, and a coverage claim that does not carry the denominator
        # reads as though the run exercised the model.
        "owners_declared_sequences": declared,
        "sequence_identities_fired": sum(
            1 for row in rows if row["kind"] == SEQUENCE_KIND
        ),
        "owner_entries": [
            {key: value for key, value in owner.items() if not key.startswith("_")}
            for owner in owners.values()
        ],
        "unresolved_identities": [
            entry for entry in resolved if entry.get("fault")
        ],
        # Every joined identity with the descriptor bytes it names. This is the
        # report's evidence payload, and it is what lets CAP4.2 and CAP4.4 read
        # a span without the install this run was joined against.
        "resolved_identities": [
            entry for entry in resolved if not entry.get("fault")
        ],
    }
    return report, owners, resolved


def _export_manifest(export_root: Path | None) -> tuple[Path | None, dict[str, Any] | None, str | None]:
    if export_root is None:
        try:
            from elysium_pipeline.paths import export_root as configured

            export_root = configured()
        except Exception as error:  # noqa: BLE001
            return None, None, f"{type(error).__name__}: {error}"
    path = Path(export_root) / EXPORT_MANIFEST
    if not path.is_file():
        return path, None, f"no export manifest at {path}"
    return path, json.loads(path.read_text(encoding="utf-8")), None


def export_arm(
    owners: dict[int, dict[str, Any]],
    resolved: list[dict[str, Any]],
    export_root: Path | None,
) -> dict[str, Any]:
    """Resolve each fired identity against what the export actually produced.

    The export carries no sequence or animation index, so the bridge is the
    label this run decoded from the installed descriptor. A label the manifest
    does not carry is attributed to the `local_sequences` rule that dropped it,
    which is why the outcomes are replayed rather than guessed.
    """
    path, manifest, reason = _export_manifest(export_root)
    if manifest is None:
        return {"available": False, "reason": reason, "path": str(path) if path else None}

    banks = manifest.get("banks", {})
    npcs = manifest.get("npcs", {})
    props = manifest.get("animated_props", {})
    cinematics = manifest.get("cinematics", {})

    # model key -> [(section, stem, {lowercased label: (label, meta)})]
    exported: dict[str, list[tuple[str, str, dict[str, tuple[str, dict[str, Any]]]]]] = {}

    def _add(section: str, stem: str, model: str, clips: dict[str, Any]) -> None:
        key = (model or "").lower()
        if not key:
            return
        registered = exported.setdefault(key, [])
        if any(existing == stem for _, existing, _ in registered):
            return
        table = {label.lower(): (label, meta) for label, meta in clips.items()}
        registered.append((section, stem, table))

    for stem, record in banks.items():
        _add("bank", stem, record.get("model", ""), record.get("clips", {}))
    for stem, record in npcs.items():
        _add("npc", stem, record.get("model", ""), record.get("own_clips", {}))
    for stem, record in props.items():
        _add("animated_prop", stem, record.get("model", ""), record.get("clips", {}))
    # A cinematic model is split into one bank per bone root; every root carries
    # the same label set, so the model resolves through the roots rather than
    # through a bank whose own `model` field already registered it above.
    for model, record in cinematics.items():
        for root in record.get("roots", []):
            bank = banks.get(root.get("bank"))
            if bank is not None:
                _add("cinematic", root["bank"], model, bank.get("clips", {}))

    # A fired base cell is what an exported clip's animation actually is, so an
    # animation identity is exported only when some fired sequence of the same
    # owner names it as its own base cell.
    base_cells: dict[int, set[int]] = {}
    for entry in resolved:
        if entry["kind"] == SEQUENCE_KIND and entry.get("descriptor"):
            base_cells.setdefault(
                int(entry["checksum"], 16), set()
            ).add(entry["descriptor"]["base_cell"])

    outcomes: list[dict[str, Any]] = []
    counts: dict[str, int] = {}
    record_counts: dict[str, int] = {}
    joined = joined_records = 0
    unavailable = unavailable_records = 0

    def _count(name: str, records: int) -> None:
        counts[name] = counts.get(name, 0) + 1
        record_counts[name] = record_counts.get(name, 0) + records

    for entry in resolved:
        checksum = int(entry["checksum"], 16)
        owner = owners[checksum]
        records = entry["records"]
        if entry.get("fault") or owner.get("_data") is None:
            unavailable += 1
            unavailable_records += records
            continue
        stems = exported.get((owner["install_key"] or "").lower(), [])
        outcome: dict[str, Any] = {
            "checksum": entry["checksum"],
            "model_name": entry["model_name"],
            "kind": entry["kind"],
            "index": entry["index"],
            "records": records,
        }
        if not stems:
            outcome["outcome"] = "export_has_no_such_owner"
            _count("export_has_no_such_owner", records)
            outcomes.append(outcome)
            continue
        outcome["stems"] = [f"{section}:{stem}" for section, stem, _ in stems]
        if entry["kind"] == ANIMATION_KIND:
            if entry["index"] in base_cells.get(checksum, set()):
                outcome["outcome"] = "joined"
                joined += 1
                joined_records += records
            else:
                outcome["outcome"] = "blend_cell_not_exported"
                _count("blend_cell_not_exported", records)
            outcomes.append(outcome)
            continue

        replay = owner["_outcomes"].get(entry["index"], {})
        label = entry["descriptor"]["label"]
        outcome["label"] = label
        if replay.get("outcome") not in (None, "kept"):
            outcome["outcome"] = replay["outcome"]
            outcome.update(
                {
                    key: value
                    for key, value in replay.items()
                    if key not in ("outcome", "label")
                }
            )
            _count(replay["outcome"], records)
            outcomes.append(outcome)
            continue
        found = None
        for section, stem, table in stems:
            if label.lower() in table:
                found = (section, stem, *table[label.lower()])
                break
        if found is None:
            outcome["outcome"] = "clip_baked_no_animation"
            _count("clip_baked_no_animation", records)
            outcomes.append(outcome)
            continue
        section, stem, exported_label, meta = found
        outcome["clip"] = {"stem": f"{section}:{stem}", "label": exported_label}
        # Falsifiable: the manifest and this run decoded the same descriptor, so
        # the five fields they both carry have to agree. A disagreement means
        # the export read those bytes differently.
        descriptor = entry["descriptor"]
        disagreements = {
            name: [ours, theirs]
            for name, ours, theirs in (
                ("activity", descriptor["activity_name"], meta.get("activity")),
                ("weight", descriptor["act_weight"], meta.get("weight")),
                ("flags", descriptor["flags"], meta.get("flags")),
            )
            if ours != theirs
        }
        if disagreements:
            outcome["outcome"] = "export_clip_meta_disagrees"
            outcome["disagreements"] = disagreements
            _count("export_clip_meta_disagrees", records)
            outcomes.append(outcome)
            continue
        outcome["outcome"] = "joined"
        joined += 1
        joined_records += records
        outcomes.append(outcome)

    return {
        "available": True,
        "path": str(path),
        "sha256": file_sha256(path),
        "manifest_version": manifest.get("manifest_version"),
        "identities": len(outcomes),
        "records": sum(outcome["records"] for outcome in outcomes),
        "joined_identities": joined,
        "joined_records": joined_records,
        "not_resolved_on_the_install_arm": unavailable,
        "not_resolved_on_the_install_arm_records": unavailable_records,
        "counts": counts,
        "records_by_count": record_counts,
        "outcomes": outcomes,
    }


def _inventory_directory(value: Path | None) -> tuple[Path | None, str | None]:
    if value is not None:
        path = Path(value)
        return (path, None) if path.is_dir() else (path, f"no inventory at {path}")
    try:
        from elysium_pipeline.paths import research_root
    except Exception as error:  # noqa: BLE001
        return None, f"{type(error).__name__}: {error}"
    root = research_root() / INVENTORY_ROOT
    runs = sorted(
        path for path in root.glob(f"{INVENTORY_PREFIX}*") if path.is_dir()
    )
    if not runs:
        return None, f"no CAP0.5 inventory under {root}"
    return runs[-1], None


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    return [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]


def inventory_arm(
    owners: dict[int, dict[str, Any]],
    resolved: list[dict[str, Any]],
    directory: Path | None,
) -> dict[str, Any]:
    """Resolve each fired identity against the CAP0.5 player inventory.

    The inventory keeps the exact owner/sequence/animation identity and the
    descriptor bytes it read, so it is the one arm that can be checked against
    the install byte for byte without either side importing the other's decode.
    """
    path, reason = _inventory_directory(directory)
    if path is None or reason:
        return {"available": False, "reason": reason, "path": str(path) if path else None}
    manifest_path = path / "manifest.json"
    manifest = (
        json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest_path.is_file()
        else {}
    )
    owner_rows = {
        row["owner_model"].lower(): row
        for row in _read_jsonl(path / "owners.jsonl")
    }
    sequence_rows = {
        (row["owner_model"].lower(), row["sequence_index"]): row
        for row in _read_jsonl(path / "sequences.jsonl")
    }
    animation_rows = {
        (row["owner_model"].lower(), row["animation_index"]): row
        for row in _read_jsonl(path / "animations.jsonl")
    }

    # Which cells the inventory declares a sequence can fire, so the capture's
    # witnessed cells can be checked against a declaration made independently.
    declared_cells: dict[str, set[int]] = {}
    for (model, _index), row in sequence_rows.items():
        declared_cells.setdefault(model, set()).update(
            cell["animation_index"] for cell in row.get("active_blend_cells", [])
        )

    outcomes: list[dict[str, Any]] = []
    counts: dict[str, int] = {}
    record_counts: dict[str, int] = {}
    joined = joined_records = 0
    unavailable = unavailable_records = 0
    unknown_bytes: list[dict[str, Any]] = []

    def _count(name: str, records: int) -> None:
        counts[name] = counts.get(name, 0) + 1
        record_counts[name] = record_counts.get(name, 0) + records

    for entry in resolved:
        checksum = int(entry["checksum"], 16)
        owner = owners[checksum]
        records = entry["records"]
        data = owner.get("_data")
        if entry.get("fault") or data is None:
            unavailable += 1
            unavailable_records += records
            continue
        model = (owner["install_key"] or "").lower()
        outcome: dict[str, Any] = {
            "checksum": entry["checksum"],
            "model_name": entry["model_name"],
            "kind": entry["kind"],
            "index": entry["index"],
            "records": records,
        }
        if model not in owner_rows:
            outcome["outcome"] = "inventory_does_not_cover_this_owner"
            _count("inventory_does_not_cover_this_owner", records)
            outcomes.append(outcome)
            continue
        if owner_rows[model].get("model_sha256") != owner["installed_sha256"]:
            outcome["outcome"] = "inventory_descriptor_digest_disagrees"
            outcome["detail"] = "the inventory read a different model image"
            _count("inventory_descriptor_digest_disagrees", records)
            outcomes.append(outcome)
            continue
        table = sequence_rows if entry["kind"] == SEQUENCE_KIND else animation_rows
        row = table.get((model, entry["index"]))
        if row is None:
            outcome["outcome"] = "inventory_missing_a_declared_index"
            _count("inventory_missing_a_declared_index", records)
            outcomes.append(outcome)
            continue
        span = row.get("source_span") or {}
        start, length = span.get("offset"), span.get("length")
        digest = None
        if isinstance(start, int) and isinstance(length, int):
            digest = hashlib.sha256(data[start : start + length]).hexdigest()
        if digest != row.get("descriptor_sha256"):
            outcome["outcome"] = "inventory_descriptor_digest_disagrees"
            outcome["detail"] = {
                "span": [start, length],
                "installed": digest,
                "inventory": row.get("descriptor_sha256"),
            }
            _count("inventory_descriptor_digest_disagrees", records)
            outcomes.append(outcome)
            continue
        if start != entry["image_offset"]:
            outcome["outcome"] = "inventory_missing_a_declared_index"
            outcome["detail"] = {
                "inventory_offset": start,
                "captured_offset": entry["image_offset"],
            }
            _count("inventory_missing_a_declared_index", records)
            outcomes.append(outcome)
            continue
        if entry["kind"] == ANIMATION_KIND and declared_cells.get(model) is not None:
            if entry["index"] not in declared_cells[model]:
                outcome["outcome"] = "fired_cell_not_declared"
                _count("fired_cell_not_declared", records)
                outcomes.append(outcome)
                continue
        outcome["outcome"] = "joined"
        outcome["identity"] = row.get("identity")
        joined += 1
        joined_records += records
        outcomes.append(outcome)
        # The unknown bytes the inventory preserved, now attached to a span the
        # runtime is witnessed to have consumed. This is CAP4.2's input.
        unknown = row.get("unknown_tail_hex") or row.get("unknown_trailing_hex")
        if unknown:
            unknown_bytes.append(
                {
                    "identity": row.get("identity"),
                    "kind": entry["kind"],
                    "records": records,
                    "unknown_hex": unknown,
                }
            )

    return {
        "available": True,
        "path": str(path),
        "manifest_sha256": file_sha256(manifest_path)
        if manifest_path.is_file()
        else None,
        "inventory_tool_git": ((manifest.get("tool") or {}).get("git") or {}).get(
            "commit"
        ),
        "inventory_owners": len(owner_rows),
        "identities": len(outcomes),
        "records": sum(outcome["records"] for outcome in outcomes),
        "joined_identities": joined,
        "joined_records": joined_records,
        "not_resolved_on_the_install_arm": unavailable,
        "not_resolved_on_the_install_arm_records": unavailable_records,
        "counts": counts,
        "records_by_count": record_counts,
        "outcomes": outcomes,
        "unknown_descriptor_bytes": unknown_bytes,
    }


def decide(
    flags: dict[str, Any],
    run: dict[str, Any],
    installed: dict[str, Any],
    export: dict[str, Any],
    inventory: dict[str, Any],
) -> dict[str, Any]:
    if not flags["carries_contributions"]:
        return {
            "judgeable": False,
            "sources_joined": False,
            "statement": (
                "This database carries no contribution stream; there is no "
                "fired identity to join."
            ),
        }
    if not flags["carries_census"]:
        return {
            "judgeable": False,
            "sources_joined": False,
            "statement": (
                "This database carries no model census, so no checksum names "
                "the model path the install is keyed on."
            ),
        }
    defects: dict[str, int] = {}
    accounted: dict[str, int] = {}
    unclassified: dict[str, int] = {}
    for arm in (installed, export, inventory):
        if not arm.get("available"):
            continue
        for name, value in (arm.get("counts") or {}).items():
            if not value:
                continue
            if name in DEFECTS:
                defects[name] = defects.get(name, 0) + value
            elif name in ACCOUNTED:
                accounted[name] = accounted.get(name, 0) + value
            else:
                # A population no declaration covers is not silently coverage.
                # Naming it here is what keeps a later class from being added
                # to an arm and quietly disappearing from the verdict.
                unclassified[name] = unclassified.get(name, 0) + value

    # The install arm is the task. An unreadable install has not joined
    # anything, so reporting it as a clean run would be reporting a run that
    # never happened. The export and the inventory are build products a fresh
    # checkout need not carry, and their absence is coverage rather than
    # failure.
    joined = not defects and not unclassified and bool(installed.get("available"))
    if unclassified:
        statement = (
            f"{len(unclassified)} populations are neither declared defects nor "
            "accounted: "
            + "; ".join(f"{name} = {value:,}" for name, value in unclassified.items())
        )
    elif defects:
        statement = (
            f"{sum(defects.values()):,} identities do not reach the bytes they "
            "name: "
            + "; ".join(f"{name} = {value:,}" for name, value in defects.items())
        )
    elif not installed.get("available"):
        statement = (
            "The install is not readable from here, so no arm could be "
            f"joined: {installed.get('reason')}"
        )
    else:
        parts = [
            f"{installed['joined_identities']:,} of {run['identities']:,} "
            f"identities ({installed['joined_records']:,} of "
            f"{run['records']:,} records) resolve to installed bytes"
        ]
        if export.get("available"):
            parts.append(
                f"{export['joined_identities']:,} reach an exported clip"
            )
        if inventory.get("available"):
            parts.append(
                f"{inventory['joined_identities']:,} reach an inventory row"
            )
        statement = (
            ", ".join(parts)
            + f"; {len(accounted)} accounted populations, no defect."
        )
    return {
        "judgeable": True,
        "sources_joined": joined,
        "defects": defects,
        "unclassified": unclassified,
        "accounted": {name: ACCOUNTED[name] for name in accounted},
        "accounted_counts": accounted,
        "arms": {
            "install": installed.get("available", False),
            "export": export.get("available", False),
            "inventory": inventory.get("available", False),
        },
        "statement": statement,
    }


def verify(
    session: Path,
    *,
    export_root: Path | None = None,
    inventory: Path | None = None,
    install_reader: Any = None,
    vtmb_root: str | None = None,
) -> dict[str, Any]:
    connection = open_database(session)
    try:
        flags = support(connection)
        metadata = {
            key: json.loads(value)
            for key, value in connection.execute(
                "SELECT key, value FROM capture_metadata WHERE key IN "
                "('map', 'created_utc', 'tool_git', 'index_version', "
                "'index_source_database_sha256', 'compact_source_database_sha256')"
            )
        }
        run: dict[str, Any] = {}
        installed: dict[str, Any] = {"available": False, "reason": "not reached"}
        export: dict[str, Any] = {"available": False, "reason": "not reached"}
        inventory_report: dict[str, Any] = {
            "available": False,
            "reason": "not reached",
        }
        owners: dict[int, dict[str, Any]] = {}
        resolved: list[dict[str, Any]] = []
        if flags["carries_contributions"]:
            prepare(connection, flags)
            run = corpus(connection)
            if flags["carries_census"]:
                installed, owners, resolved = install_arm(
                    connection, flags, install_reader, vtmb_root
                )
                export = export_arm(owners, resolved, export_root)
                inventory_report = inventory_arm(owners, resolved, inventory)
        verdict = decide(flags, run, installed, export, inventory_report)
    finally:
        connection.close()
    return {
        "session": session.name,
        "session_path": str(session),
        "database": str(session / DATABASE_NAME),
        "identity": {
            **metadata,
            "join_tool_git": tool_commit(),
            "vtmb_root": installed.get("vtmb_root"),
        },
        "support": flags,
        "corpus": run,
        "install": installed,
        "export": export,
        "inventory": inventory_report,
        "verdict": verdict,
    }


def compare(reports: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "sessions": [report["session"] for report in reports],
        "maps": compared_maps(reports),
        "identities": [
            (report["corpus"] or {}).get("identities") for report in reports
        ],
        "joined_identities": [
            report["install"].get("joined_identities") for report in reports
        ],
        "statement": (
            "Which identities a run fires is a property of its own scene and "
            "is reported side by side rather than compared. What a repeat run "
            "supports is that the identities it shares resolve the same way."
        ),
    }


def summarize(report: dict[str, Any]) -> str:
    lines = [f"{report['session']}: {report['verdict']['statement']}"]
    run = report.get("corpus") or {}
    if run:
        lines.append(
            f"  fired: {run['owners']} owners, {run['identities']} identities "
            f"({run['sequence']['identities']} sequence, "
            f"{run['animation']['identities']} animation), "
            f"{run['records']:,} records"
        )
    installed = report.get("install") or {}
    if installed.get("available"):
        lines.append(
            f"  install: {installed['joined_identities']}/"
            f"{installed['identities']} identities, "
            f"{installed['owners_resolved']}/{installed['owners']} owners, "
            f"firing {installed['sequence_identities_fired']} of "
            f"{installed['owners_declared_sequences']} declared sequences"
        )
        for population, value in sorted(
            ((name, count) for name, count in installed["counts"].items() if count),
            key=lambda item: -item[1],
        ):
            lines.append(
                f"    {population}: {value} identities, "
                f"{installed['records_by_count'][population]:,} records"
            )
        lines.append(
            "    descriptor bytes against the captured image: "
            + ", ".join(
                f"{name} {count}"
                for name, count in sorted(installed["descriptor_bytes"].items())
            )
        )
    for name, arm in (("export", report.get("export")), ("inventory", report.get("inventory"))):
        arm = arm or {}
        if not arm.get("available"):
            lines.append(f"  {name}: unavailable ({arm.get('reason')})")
            continue
        lines.append(
            f"  {name}: {arm['joined_identities']}/{arm['identities']} "
            f"identities, {arm['joined_records']:,}/{arm['records']:,} records"
        )
        for population, value in sorted(
            (arm.get("counts") or {}).items(), key=lambda item: -item[1]
        ):
            lines.append(
                f"    {population}: {value} identities, "
                f"{arm['records_by_count'][population]:,} records"
            )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--export-root", type=Path)
    parser.add_argument("--inventory", type=Path)
    parser.add_argument(
        "--no-session-reports", dest="session_reports", action="store_false"
    )
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        report = verify(
            session, export_root=args.export_root, inventory=args.inventory
        )
        reports.append(report)
        if args.session_reports:
            (session / REPORT_NAME).write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8"
            )
        print(summarize(report))
    if args.report:
        args.report.write_text(
            json.dumps(
                {
                    "sessions": [report["session"] for report in reports],
                    "reports": reports,
                    "comparison": compare(reports) if len(reports) > 1 else None,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    return (
        0
        if all(report["verdict"]["sources_joined"] for report in reports)
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
